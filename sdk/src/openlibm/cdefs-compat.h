/* cdefs-compat.h -- minimal for the HP Prime port: all aliasing macros
 * disabled (we rename functions via -Dsin=hp_sin etc. and do not want
 * the sinl/cosl/... weak aliases). */
#ifndef _CDEFS_COMPAT_H_
#define _CDEFS_COMPAT_H_
#define __BEGIN_DECLS
#define __END_DECLS
#define openlibm_strong_reference(sym, aliassym)
#define openlibm_weak_reference(sym, alias)
#define openlibm_warn_references(sym, msg)
#define OLM_DLLEXPORT
#endif
