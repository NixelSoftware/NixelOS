#ifndef SYNDIG_DIAGNOSTICS_H
#define SYNDIG_DIAGNOSTICS_H
/* No hosted stdio available in ring 3. */
#define printf(...) ((void)0)
#endif
