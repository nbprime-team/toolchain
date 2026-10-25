# -*- coding: utf-8 -*-
"""
HP Prime 通用二进制引导器 (generic_loader.py)
=============================================
从 DOOM 移植项目 (main.py) 抽象出的通用版本：
你只需要改文件末尾的 APP 配置字典，就能加载并运行你自己的 ELF 程序。

用法
----
1. 把你的程序编译成符合约定的 ELF (见 README 第 2/4 节)：
   - EM_ARM, ET_DYN (PIC), vaddr 从 0 开始
   - 入口签名: void* app_entry(BootConfig* cfg)  (R0=cfg, R1=0)
   - 返回 LogStruct* 供日志回显, 失败返回 NULL
2. 把 ELF 和本脚本放进同一个 .hpappdir 目录 (如 C:\\DATA\\MYAPP.hpappdir)
3. 编辑末尾 APP 字典，在 HP Prime 上运行本脚本。

依赖后门
--------
固件内存调试接口 (debug 文件) + 导出函数表 (BASE)。BASE 按固件版本调整。
"""

try:
    import ustruct as struct
    import uio
    import hpprime
except ImportError:
    import struct
    import io as uio


# ============================================================
# 1. 后门访问层
# ============================================================
class PrimeDebug:
    """封装固件内存调试接口: 读内存 / 写内存 / 任意函数调用。"""

    def __init__(self, filename="debug"):
        try:
            self.f = uio.FileIO("debug")
        except Exception:
            self.f = open(filename, "rb")
        print("[+] Debug interface opened.")

    def close(self):
        if self.f:
            self.f.close()

    def write_mem(self, addr, val):
        """写一个 32 位小端值到 addr。"""
        self.f.write(struct.pack("<III", 1, addr, val))

    def read_mem(self, addr, size):
        """从 addr 读 size 字节。"""
        self.f.write(struct.pack("<III", 0, addr, 0))
        return self.f.read(size)

    def call(self, func_addr, *args):
        """以 args 为参数调用 func_addr, 返回 R0 (函数返回值)。

        协议: 写入 [命令=2][func_addr][argc][arg0..argN],
        固件执行后把返回值写回缓冲区第一个字段。
        """
        arg_count = len(args)
        fmt = "<III" + "I" * arg_count
        buf = bytearray(struct.calcsize(fmt))
        struct.pack_into(fmt, buf, 0, 2, func_addr, arg_count, *args)
        self.f.write(buf)
        return struct.unpack_from("<I", buf, 0)[0]

    def write_mem_bytes(self, addr, data):
        """按 4 字节对齐把任意字节串写入内存。"""
        pad_len = (4 - (len(data) % 4)) % 4
        data += b'\x00' * pad_len
        for i in range(0, len(data), 4):
            val = struct.unpack("<I", data[i:i + 4])[0]
            self.write_mem(addr + i, val)


# ============================================================
# 2. 固件符号定位
# ============================================================
# 固件内部导出表基址 (按你的固件版本调整；见 README 步骤 1)
BASE = 0x307FBCAC


def get_addr(int_id):
    """从导出表取函数地址: 每项 12 字节, ID 从 0x10000 起。"""
    return BASE + 0xC * (int_id - 0x10000)


ADDR_MALLOC = get_addr(0x10037)  # malloc
ADDR_FREE   = get_addr(0x1003A)  # free


# ============================================================
# 3. ELF 工具
# ============================================================
class ElfTools:
    """ELF 解析与字符串转换工具。"""

    @staticmethod
    def str_to_utf16le_bytes(s):
        """字符串 → UTF-16LE 字节流 (无 BOM, 结尾双 NULL)。

        MicroPython 的 encode 只支持 UTF-8, 而底层文件 API 要宽字符串,
        所以手动逐字符转换。
        """
        res = bytearray()
        for char in s:
            val = ord(char)
            res.append(val & 0xFF)
            res.append((val >> 8) & 0xFF)
        res.append(0)
        res.append(0)
        return bytes(res)

    @staticmethod
    def get_elf_memory_size(filename):
        """只读 ELF 头, 计算加载所需总内存 (max(vaddr+memsz) 对齐)。"""
        required_size = 0
        try:
            with uio.FileIO(filename, "rb") as f:
                ehdr = f.read(52)
                if len(ehdr) < 52 or ehdr[0:4] != b'\x7fELF':
                    print("[-] Invalid ELF header")
                    return 0

                e_phoff = struct.unpack_from("<I", ehdr, 28)[0]
                e_phentsize = struct.unpack_from("<H", ehdr, 42)[0]
                e_phnum = struct.unpack_from("<H", ehdr, 44)[0]

                f.seek(e_phoff)
                PT_LOAD = 1
                for i in range(e_phnum):
                    ph_data = f.read(e_phentsize)
                    if len(ph_data) < e_phentsize:
                        break
                    p_type = struct.unpack_from("<I", ph_data, 0)[0]
                    p_vaddr = struct.unpack_from("<I", ph_data, 8)[0]
                    p_memsz = struct.unpack_from("<I", ph_data, 20)[0]
                    if p_type == PT_LOAD:
                        end_addr = p_vaddr + p_memsz
                        if end_addr > required_size:
                            required_size = end_addr
        except Exception as e:
            print("[-] Error parsing ELF: " + str(e))
            return 0
        return (required_size + 3) & ~3


# ============================================================
# 4. Shellcode ELF 加载器
#    (与 DOOM 项目同一份已验证的 ARM 机器码: 读文件→拷段→清bss→返入口)
# ============================================================
class ShellcodeElfLoader:
    def __init__(self, dbg):
        self.dbg = dbg
        self.loader_addr = 0
        self.loaded_elf_base = 0
        self.entry_point = 0
        # 编译好的 loader.bin (通用 ELF 段加载器, 与项目约定一致)
        self.loader_code = b'\x0b\x00\x00\xea\x04\x00-\xe5\x04\xe0-\xe5o\x02\x01\xef\x04\x00-\xe5\x04\xe0-\xe5\xca\x00\x01\xef\x04\x00-\xe5\x04\xe0-\xe5\xd4\x00\x01\xef\x04\x00-\xe5\x04\xe0-\xe5\xcf\x00\x01\xefh2\x9f\xe5\xf0O-\xe9\x030\x8f\xe0\x00 \xa0\xe1\x01`\xa0\xe1\x03\x00\x93\xe8d\xd0M\xe2\x04\x00\x8d\xe5\xb8\x10\xcd\xe1\x02\x00\xa0\xe1\x04\x10\x8d\xe2\xe7\xff\xff\xeb\x00@P\xe2s\x00\x00\n\x040\xa0\xe1\x01 \xa0\xe34\x10\xa0\xe3,\x00\x8d\xe2\xe6\xff\xff\xeb\x01\x00P\xe3j\x00\x00\x1a\xbc"\xdd\xe1\x142\x9f\xe5\x03\x00R\xe1f\x00\x00\x1aH\x10\x9d\xe5\x00 \xa0\xe3\x04\x00\xa0\xe1\xdf\xff\xff\xeb\xb85\xdd\xe1\x00\x00S\xe3x\x00\x00\n\x00P\xa0\xe3\x05p\xa0\xe1\x0c\xa0\x8d\xe2\x04\x00\x00\xea\xb85\xdd\xe1\x02\x00[\xe3\x14p\x9d\x05\x05\x00S\xe1\x19\x00\x00\xda\x040\xa0\xe1\x01 \xa0\xe3 \x10\xa0\xe3\n\x00\xa0\xe1\xcb\xff\xff\xeb\x0c\xb0\x9d\xe5\x01P\x85\xe2\x01\x00[\xe3\xf1\xff\xff\x1a\x1c0\x9d\xe5\x14\x80\x9d\xe5H\x90\x9d\xe5\x00\x00S\xe3\x08\x80\x86\xe0\x85\x92\x89\xe0K\x00\x00\x1a  \x9d\xe5\x03\x00R\xe1U\x00\x00\x8a\t\x10\xa0\xe1\x00 \xa0\xe3\x04\x00\xa0\xe1\xbc\xff\xff\xeb\xb85\xdd\xe1\x05\x00S\xe1\xe5\xff\xff\xca\x04\x00\xa0\xe1\xb1\xff\xff\xeb\x00\x00W\xe3/\x00\x00\n\x070\x96\xe7\x07p\x86\xe0\x00\x00S\xe3+\x00\x00\n\x00 \xa0\xe3\x08\xc0\xa0\xe3\x02\x00\xa0\xe1\x02\xe0\xa0\xe1\x11\x00S\xe3\x04\xe0\x97\x05\x04\x00\x00\n\x12\x00S\xe3\x04\x00\x97\x05\x01\x00\x00\n\x13\x00S\xe3\x04\xc0\x97\x05\x080\xb7\xe5\x01 \x82\xe2\x00\x00S\xe3d\x00R\x13\x01\x10\xa0\x13\x00\x10\xa0\x03\xf0\xff\xff\x1a\x00\x00^\xe3\x00\x00P\x13\x15\x00\x00\n\x00\x00\\\xe3\x08\xc0\xa0\x03\x0c\x00P\xe1\x11\x00\x00:\x0c\x00@\xe0\x00\x00\\\xe1\x01\x10\x81\xe2\xfb\xff\xff\x9a\x00\x00Q\xe3\x0b\x00\x00\n\x81\x11\x86\xe0\x0e0\x86\xe0\x0e\x10\x81\xe0\x04 \xd3\xe5\x080\x83\xe2\x17\x00R\xe3\x08\x00\x13\x05\x00 \x96\x07\x02 \x86\x00\x00 \x86\x07\x01\x00S\xe1\xf6\xff\xff\x1a\x000\xa0\xe3~\xff\x17\xee\xfd\xff\xff\x1a\x9a?\x07\xee\x15?\x07\xeeD0\x9d\xe5\x03\x00\x86\xe0d\xd0\x8d\xe2\xf0\x8f\xbd\xe8\x04\x00\xa0\xe1t\xff\xff\xeb\x00\x00\xa0\xe3d\xd0\x8d\xe2\xf0\x8f\xbd\xe8\x10\x10\x9d\xe5\x00 \xa0\xe3\x04\x00\xa0\xe1s\xff\xff\xeb\x1c \x9d\xe5\x040\xa0\xe1\x0b\x10\xa0\xe1\x08\x00\xa0\xe1k\xff\xff\xeb\x1c0\x9d\xe5  \x9d\xe5\x03\x00R\xe1\xa9\xff\xff\x9a\x030\x88\xe0\x02\x80\x88\xe0\x00 \xa0\xe3\x01 \xc3\xe4\x08\x00S\xe1\xfc\xff\xff\x1a\xa2\xff\xff\xea\x04\x00\xa0\xe1[\xff\xff\xeb\xda\xff\xff\xeah\x02\x00\x00\x7fE\x00\x00r\x00b\x00\x00\x00'

    def upload_loader(self):
        """上传 shellcode 加载器到 malloc 内存。"""
        size = len(self.loader_code)
        if size == 0:
            print("[!] Warning: loader_code is empty!")
            return False
        print("[*] Allocating loader shellcode ({} bytes)...".format(size))
        self.loader_addr = self.dbg.call(ADDR_MALLOC, (size + 3) & ~3)
        if self.loader_addr == 0:
            print("[-] Malloc failed for loader")
            return False
        print("[*] Uploading loader to 0x{:08X}".format(self.loader_addr))
        self.dbg.write_mem_bytes(self.loader_addr, self.loader_code)
        return True

    def load_elf(self, filename, app_dir):
        """用 shellcode 加载 ELF: 计算大小→分配→传 UTF-16 路径→调用→返入口。"""
        print("[*] Loading ELF using shellcode: {}".format(filename))

        mem_size = ElfTools.get_elf_memory_size(filename)
        print("[*] ELF requires memory: {} bytes".format(mem_size))
        if mem_size == 0:
            return None

        self.loaded_elf_base = self.dbg.call(ADDR_MALLOC, mem_size)
        print("[*] Allocated Target Memory at: 0x{:08X}".format(self.loaded_elf_base))
        if self.loaded_elf_base == 0:
            print("[-] Target malloc failed")
            return None

        path_bytes = ElfTools.str_to_utf16le_bytes(app_dir + "\\" + filename)
        path_addr = self.dbg.call(ADDR_MALLOC, len(path_bytes))
        self.dbg.write_mem_bytes(path_addr, path_bytes)

        try:
            print("[*] Calling shellcode loader...")
            self.entry_point = self.dbg.call(self.loader_addr, path_addr, self.loaded_elf_base)
            print("[+] Shellcode returned Entry Point: 0x{:08X}".format(self.entry_point))

            if self.entry_point == 0 or self.entry_point < 0x30000000:
                print("[-] Loading failed (Entry point suspicious)")
                return None
            return self.entry_point
        finally:
            if path_addr:
                self.dbg.call(ADDR_FREE, path_addr)

    def unload(self):
        """卸载 ELF 与 shellcode。"""
        if self.loaded_elf_base:
            self.dbg.call(ADDR_FREE, self.loaded_elf_base)
            print("[+] ELF Memory Freed")
            self.loaded_elf_base = 0
            self.entry_point = 0
        if self.loader_addr:
            self.dbg.call(ADDR_FREE, self.loader_addr)
            print("[+] Shellcode Memory Freed")
            self.loader_addr = 0


# ============================================================
# 5. 日志回显 (C 端环形缓冲)
# ============================================================
class LogReader:
    def __init__(self, debug_interface, log_struct_addr, log_size=4096):
        self.dbg = debug_interface
        self.addr = log_struct_addr
        self.local_head = 0
        self.log_size = log_size  # 必须与 C 端 LOG_BUFFER_SIZE 一致

    def process(self):
        try:
            head_bytes = self.dbg.read_mem(self.addr, 4)
            remote_head = struct.unpack("<I", head_bytes)[0]
        except Exception as e:
            print("[Log] Read Error:", e)
            return

        if remote_head > self.local_head:
            start_idx = self.local_head % self.log_size
            end_idx = remote_head % self.log_size
            new_data = b""

            if end_idx > start_idx:
                new_data = self.dbg.read_mem(self.addr + 4 + start_idx, end_idx - start_idx)
            else:
                len1 = self.log_size - start_idx
                if len1 > 0:
                    new_data += self.dbg.read_mem(self.addr + 4 + start_idx, len1)
                if end_idx > 0:
                    new_data += self.dbg.read_mem(self.addr + 4, end_idx)

            try:
                print(new_data.decode('utf-8', 'ignore'), end='')
            except Exception:
                print(new_data)
            self.local_head = remote_head


# ============================================================
# 6. 启动配置构造 (BootConfig)
# ============================================================
class BootConfigBuilder:
    """在计算器内存中按 ABI 构造 C 端启动配置结构体。

    布局: {magic, argc, argv, env_count, envs, key_count, keys, services}
    (32 字节, 8 字段; services 为 PrimeServices* 或 0)
    keys 每项 {uint16 hp_scan, uint16 app_key}
    """

    def __init__(self, dbg, spec):
        self.dbg = dbg
        self.spec = spec
        self.allocations = []

    def _alloc_str(self, s):
        b = s.encode('utf-8') + b'\x00'
        size = (len(b) + 3) & ~3
        addr = self.dbg.call(ADDR_MALLOC, size)
        self.dbg.write_mem_bytes(addr, b)
        self.allocations.append(addr)
        return addr

    def _alloc_ptr_array(self, pointers):
        size = len(pointers) * 4
        if size == 0:
            return 0
        addr = self.dbg.call(ADDR_MALLOC, size)
        data = b''
        for p in pointers:
            data += struct.pack("<I", p)
        self.dbg.write_mem_bytes(addr, data)
        self.allocations.append(addr)
        return addr

    def build(self):
        print("[*] Building configuration structure...")

        # --- argv ---
        arg_ptrs = [self._alloc_str(x) for x in self.spec.get("args", [])]
        argv_addr = self._alloc_ptr_array(arg_ptrs)

        # --- envs: {char* name; char* val;} x n ---
        env_array_addr = 0
        envs = self.spec.get("envs", {})
        if envs:
            env_array_addr = self.dbg.call(ADDR_MALLOC, len(envs) * 8)
            self.allocations.append(env_array_addr)
            i = 0
            for k, v in envs.items():
                k_addr = self._alloc_str(k)
                v_addr = self._alloc_str(v)
                self.dbg.write_mem(env_array_addr + i * 8, k_addr)
                self.dbg.write_mem(env_array_addr + i * 8 + 4, v_addr)
                i += 1

        # --- keys: {uint16 hp, uint16 app} x n ---
        key_array_addr = 0
        keys = self.spec.get("keys", [])
        if keys:
            key_array_addr = self.dbg.call(ADDR_MALLOC, len(keys) * 4)
            self.allocations.append(key_array_addr)
            data = b''
            for hp_code, app_code in keys:
                data += struct.pack("<HH", hp_code, app_code)
            self.dbg.write_mem_bytes(key_array_addr, data)

        # --- 主结构体 ---
        magic = self.spec.get("magic", 0xD00BC760)
        # --- 系统服务表 (PrimeServices, 9 x uint32) ---
        # 布局与移植工程的 port/prime_sys.h 一致:
        #   [0]=malloc(固件) [1]=free(固件) [2]=framebuffer 地址 (0=自动)
        #   [3]=fb宽 [4]=fb高 [5]=fb行字节 [6]=clock_ms [7]=key_read
        #   [8]=display_flags: bit0=flip_y bit1=force_rotate bit2=no_center
        # 服务表总是构造 (display_flags 是 ELF 读取显示布局配置的通道;
        # framebuffer addr=0 时 ELF 用 SVC 自动获取)。
        services_addr = 0
        fb = self.spec.get("framebuffer", None)
        if fb is not None:
            fb_addr = fb.get("addr", 0)
            fb_w = fb.get("width", 0) if fb_addr != 0 else 0
            fb_h = fb.get("height", 0) if fb_addr != 0 else 0
            fb_stride = fb.get("stride_bytes", 0) if fb_addr != 0 else 0
            flags = fb.get("flags", 0)
            services_data = struct.pack("<IIIIIIIII",
                                        ADDR_MALLOC,
                                        ADDR_FREE,
                                        fb_addr,
                                        fb_w,
                                        fb_h,
                                        fb_stride,
                                        0,  # clock_ms (可扩展)
                                        0,  # key_read (可扩展)
                                        flags)
            services_addr = self.dbg.call(ADDR_MALLOC, len(services_data))
            self.dbg.write_mem_bytes(services_addr, services_data)
            self.allocations.append(services_addr)

        payload = struct.pack("<IIIIIIII",
                              magic,
                              len(self.spec.get("args", [])),
                              argv_addr,
                              len(envs),
                              env_array_addr,
                              len(keys),
                              key_array_addr,
                              services_addr)
        config_addr = self.dbg.call(ADDR_MALLOC, 32)
        self.dbg.write_mem_bytes(config_addr, payload)
        self.allocations.append(config_addr)

        print("[+] Config built at: 0x{:08X}".format(config_addr))
        return config_addr

    def free_all(self):
        for addr in reversed(self.allocations):
            self.dbg.call(ADDR_FREE, addr)
        self.allocations = []


# ============================================================
# 7. 主流程
# ============================================================
def run_app(spec):
    """加载并运行一个应用。spec 为配置字典, 见文件末尾 APP。"""
    name = spec.get("name", "app")
    print("--- HP Prime Generic Loader: {} ---".format(name))

    dbg = None
    loader = None
    builder = None
    try:
        dbg = PrimeDebug()
        loader = ShellcodeElfLoader(dbg)

        if not loader.upload_loader():
            print("[-] Failed to upload shellcode loader")
            return

        entry_point = loader.load_elf(spec["elf"], spec.get("dir", "C:\\DATA"))
        if not entry_point:
            return

        builder = BootConfigBuilder(dbg, spec)
        config_addr = builder.build()

        print("[*] Calling entry point (0x{:08X}) with config...".format(entry_point))
        input("Press Enter to Launch...")

        log_addr = dbg.call(entry_point, config_addr, 0)
        print("[+] App init returned. Log Structure at: 0x{:08X}".format(log_addr))

        if spec.get("entry_returns_log", True):
            if log_addr == 0 or log_addr < 0x30000000:
                print("[!] Invalid log address returned. Did C code crash early?")
                return
            logger = LogReader(dbg, log_addr, spec.get("log_buffer_size", 4096))
            print("--- Streaming Logs (Press Ctrl+C to stop) ---")
            while True:
                logger.process()
                t = hpprime.ticks()
                while hpprime.ticks() < t + 100:
                    pass
        else:
            # 应用不返回日志结构体: 认为入口调用本身即一次启动, 直接结束
            print("[+] Entry called. App running independently.")
            print("    (entry_returns_log=False, no log streaming)")

    except KeyboardInterrupt:
        print("\n[!] Interrupted by user.")
    except Exception as e:
        print("[!!] An error occurred: " + str(e))
    finally:
        if builder is not None:
            builder.free_all()
        if loader is not None:
            loader.unload()
        if dbg is not None:
            dbg.close()
        print("--- Loader has shut down. ---")


# ============================================================
# 8. 应用配置 —— 改这里!
# ============================================================
# 默认保留 DOOM 配置作链路验证 (与原始 main.py 等价)
APP = {
    "name":    "doom",
    "elf":     "puredoom.elf",
    "dir":     "C:\\DATA\\DOOM.hpappdir",
    "args":    ["doom", "-iwad", "C:\\DATA\\DOOM.hpappdir\\doom1.wad"],
    "envs":    {"HOME": "C:\\DATA\\DOOM.hpappdir",
                "DOOMWADDIR": "C:\\DATA\\DOOM.hpappdir"},
    "keys":    [
        # (HP 物理扫描码, 程序键 ID) —— 见 main.py 的 HP_KEY / DOOM_KEY 定义
        (0x03, 0xAD),  # UP    -> 上
        (0x05, 0xAF),  # DOWN  -> 下
        (0x02, 0xAC),  # LEFT  -> 左
        (0x04, 0xAE),  # RIGHT -> 右
        (0x0D, 0x9D),  # ENTER -> CTRL (开火)
        (0x20, 0x20),  # SPACE -> 使用
        (0x8B, 0xB6),  # SHIFT -> 跑
        (0xB6, 0xB8),  # ALPHA -> ALT (平移)
        (0x01, 0x1B),  # ESC   -> ESC
        (0x0C, 0x7F),  # BS    -> 后退
        (0xB1, 0x09),  # APPS  -> TAB (自动地图)
        (0xB9, 0x0D),  # +     -> ENTER
        (0x91, 0x79),  # F1    -> Y
        (0x93, 0x6E),  # F2    -> N
        (0x59, 0x31),  # 1     -> 武器1
        (0x5A, 0x32),  # 2     -> 武器2
        (0x33, 0x33),  # 3     -> 武器3
        (0x55, 0x34),  # 4     -> 武器4
        (0x56, 0x35),  # 5     -> 武器5
        (0x57, 0x36),  # 6     -> 武器6
        (0x51, 0x37),  # 7     -> 武器7
    ],
    "entry_returns_log": True,   # 入口返回 LogStruct* → 开启日志轮询
    "log_buffer_size": 4096,     # 与 C 端 LOG_BUFFER_SIZE 一致
    "magic": 0xD00BC760,         # 与 C 端魔数校验一致
    # 系统服务表 (可选): ELF 端已通过 Besta OS SVC 直调获取 LCD framebuffer
    # 与内存分配 (见 hp-prime-nmarkdown/port/prime_svc.h), 因此 framebuffer
    # 地址不再必需 —— 保持 addr=0 即可。仅当 SVC 不可用 (模拟环境) 时,
    # 才需要填写 framebuffer 地址 (此时假定 RGB565 布局)。
    "framebuffer": {
        "addr": 0x00000000,
        "width": 320,
        "height": 240,
        "stride_bytes": 320 * 2,
    },
}

run_app(APP)
