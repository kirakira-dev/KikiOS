/*
 * Windows Executable Runner for KikiOS
 * 
 * Header file for winexec functions
 */

#ifndef WINEXEC_H
#define WINEXEC_H

// Run a Windows .exe file
// Returns exit code, or -1 on error
int winexec_run(const char *path);

// Check if Windows executable support is available
// Returns 1 if supported, 0 otherwise
int winexec_supported(void);

#endif
