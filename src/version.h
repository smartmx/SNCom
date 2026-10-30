/* version.h - single source of truth for the SNCom version number.
 *
 * Shared by the C++ sources (src/ui/mainwindow.cpp) and by sncom.rc
 * (rc.exe includes this file too, so it must stay **ASCII-only**:
 * no non-ASCII bytes, no BOM - rc would decode them as ANSI).
 *
 * To bump the version: change the four numbers, SNCOM_VERSION_FILE and the
 * two strings **together**, then add a CHANGELOG.md entry. Consistency
 * (numbers vs strings vs the binary's VERSIONINFO resource vs the UI) is
 * guarded by probes/verify_r109.py.
 */
#ifndef SNCOM_VERSION_H
#define SNCOM_VERSION_H

#define SNCOM_VERSION_MAJOR 0
#define SNCOM_VERSION_MINOR 0
#define SNCOM_VERSION_PATCH 1
#define SNCOM_VERSION_BUILD 0

/* rc.exe VERSIONINFO numeric fields (must equal the four numbers above) */
#define SNCOM_VERSION_FILE 0,0,1,0

/* display strings (must equal the numbers above) */
#define SNCOM_VERSION_STR4 "0.0.1.0"        /* four-part, for the resource */
#define SNCOM_VERSION_STR3 "0.0.1"          /* three-part, for the UI       */

#endif /* SNCOM_VERSION_H */
