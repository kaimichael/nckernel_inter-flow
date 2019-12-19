#ifndef _NCK_CONFIG_H_
#define _NCK_CONFIG_H_

#define NCKERNEL_VERSION "0.6.377"
#define NCKERNEL_REVISION "8b9b17f"
#define NCKERNEL_BUILD "RelWithDebInfo"

const char *nck_version();
const char *nck_revision();

#define ENABLE_NOCODE
/* #undef ENABLE_REP */
#define ENABLE_NOACK
#define ENABLE_GACK
#define ENABLE_GSAW
#define ENABLE_PACE
#define ENABLE_PACEMG
#define ENABLE_CODARQ
#define ENABLE_TETRYS
#define ENABLE_INTERFLOW_SLIDING_WINDOW
#define ENABLE_SLIDING_WINDOW
#define ENABLE_CHAIN

#endif /* _NCK_CONFIG_H_ */
