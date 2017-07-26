/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2008 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2017 Cisco Systems, Inc.  All rights reserved
 * Copyright (c) 2007-2009 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2007-2017 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2013-2017 Intel, Inc.  All rights reserved.
 * Copyright (c) 2015      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orte_config.h"
#include "orte/constants.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif  /* HAVE_STRINGS_H */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif  /* HAVE_SYS_TYPES_H */
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif  /* HAVE_SYS_WAIT_H */
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif  /* HAVE_SYS_TIME_H */
#include <fcntl.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "opal/mca/event/event.h"
#include "opal/mca/installdirs/installdirs.h"
#include "opal/mca/hwloc/base/base.h"
#include "opal/mca/base/base.h"
#include "opal/util/argv.h"
#include "opal/util/output.h"
#include "opal/util/basename.h"
#include "opal/util/cmd_line.h"
#include "opal/util/opal_environ.h"
#include "opal/util/opal_getcwd.h"
#include "opal/util/show_help.h"
#include "opal/util/fd.h"
#include "opal/sys/atomic.h"
#if OPAL_ENABLE_FT_CR == 1
#include "opal/runtime/opal_cr.h"
#endif

#include "opal/version.h"
#include "opal/runtime/opal.h"
#include "opal/runtime/opal_info_support.h"
#include "opal/util/os_path.h"
#include "opal/util/path.h"
#include "opal/class/opal_pointer_array.h"
#include "opal/dss/dss.h"

#include "orte/mca/dfs/dfs.h"
#include "orte/mca/odls/odls.h"
#include "orte/mca/rml/rml.h"
#include "orte/mca/state/state.h"
#include "orte/util/proc_info.h"
#include "orte/util/session_dir.h"
#include "orte/util/show_help.h"
#include "orte/util/threads.h"

#include "orte/runtime/runtime.h"
#include "orte/runtime/orte_globals.h"
#include "orte/runtime/orte_wait.h"
#include "orte/runtime/orte_locks.h"
#include "orte/runtime/orte_quit.h"

/* ensure I can behave like a daemon */
#include "orte/orted/orted.h"
#include "orte/orted/orted_submit.h"
#include "prun.h"

/* local type */
 typedef struct {
     int status;
     volatile bool active;
     orte_job_t *jdata;
 } orte_submit_status_t;


static void launched(int index, orte_job_t *jdata, int ret, void *cbdata)
{
    orte_submit_status_t *launchst = (orte_submit_status_t*)cbdata;
    launchst->status = ret;
    ORTE_UPDATE_EXIT_STATUS(ret);
    OBJ_RETAIN(jdata);
    launchst->jdata = jdata;
    launchst->active = false;
}
static void completed(int index, orte_job_t *jdata, int ret, void *cbdata)
{
    orte_submit_status_t *completest = (orte_submit_status_t*)cbdata;
    completest->status = ret;
    ORTE_UPDATE_EXIT_STATUS(ret);
    OBJ_RETAIN(jdata);
    completest->jdata = jdata;
    completest->active = false;
}

#define PRUN_LOOP_LIMIT  10

int prun(int argc, char *argv[])
{
    orte_submit_status_t launchst, completest;
    int n;
    char *filename, *tdir;
    bool init_done = false;

    /* ****************************************************************/
    /* we want to be able to detect that the PSRVR is up and running
     * prior to attempting to connect to it. The code logic in ORTE
     * actually supports such things, but unfortunately will emit
     * error messages about the contact file missing before we can
     * arrive at the point where retries can be done. So...let's
     * setup the path to the contact file here, check to see if it
     * exists, and then cycle a while if it doesn't. */

    if (NULL != (tdir = getenv("PMIX_SERVER_TMPDIR"))) {
        filename = opal_os_path(false, tdir, "contact.txt", NULL);
        if (NULL == filename) {
            fprintf(stderr, "OUT OF MEMORY\n");
            exit(1);
        }
    } else {
        /* setup the param system */
        if (OPAL_SUCCESS != opal_init(&argc, &argv)) {
            fprintf(stderr, "Cannot setup OPAL\n");
            exit(1);
        }
        init_done = true;
        /* get the nodename */
        orte_proc_info();

        /* setup the top session directory name */
        if (ORTE_SUCCESS != orte_setup_top_session_dir()) {
            fprintf(stderr, "OUT OF MEMORY\n");
            exit(1);
        }
        filename = opal_os_path(false, orte_process_info.top_session_dir, "dvm", "contact.txt", NULL);
        if (NULL == filename) {
            fprintf(stderr, "OUT OF MEMORY\n");
            exit(1);
        }
    }
    /* check to see if the file exists - loop a few times if it
     * doesn't, delaying between successive attempts */
    n = 0;
    while (n < PRUN_LOOP_LIMIT &&
           0 != access(filename, R_OK)) {
        sleep(1);
        ++n;
    }
    /* if we still don't see the file, then give up */
    if (0 != access(filename, R_OK)) {
        fprintf(stderr, "PSRVR contact file %s not found - cannot continue\n", filename);
        exit(1);
    }
    free(filename);

    /* orte_submit_init() will also check if the user is running as
       root (and may issue a warning/exit). */
    if (ORTE_SUCCESS != orte_submit_init(argc, argv, NULL)) {
        exit(1);
    }
    if (init_done) {
        /* refcount opal_init */
        opal_finalize();
    }

    /* check if we are running as root - if we are, then only allow
     * us to proceed if the allow-run-as-root flag was given. Otherwise,
     * exit with a giant warning flag
     */
    if (0 == geteuid() && !orte_cmd_options.run_as_root) {
        fprintf(stderr, "--------------------------------------------------------------------------\n");
        if (NULL != orte_cmd_options.help) {
            fprintf(stderr, "%s cannot provide the help message when run as root.\n", orte_basename);
        } else {
            /* show_help is not yet available, so print an error manually */
            fprintf(stderr, "%s has detected an attempt to run as root.\n", orte_basename);
        }
        fprintf(stderr, "Running at root is *strongly* discouraged as any mistake (e.g., in\n");
        fprintf(stderr, "defining TMPDIR) or bug can result in catastrophic damage to the OS\n");
        fprintf(stderr, "file system, leaving your system in an unusable state.\n\n");
        fprintf(stderr, "You can override this protection by adding the --allow-run-as-root\n");
        fprintf(stderr, "option to your cmd line. However, we reiterate our strong advice\n");
        fprintf(stderr, "against doing so - please do so at your own risk.\n");
        fprintf(stderr, "--------------------------------------------------------------------------\n");
        exit(1);
    }

    /* setup to listen for commands sent specifically to me, even though I would probably
     * be the one sending them! Unfortunately, since I am a participating daemon,
     * there are times I need to send a command to "all daemons", and that means *I* have
     * to receive it too
     */
    orte_rml.recv_buffer_nb(ORTE_NAME_WILDCARD, ORTE_RML_TAG_DAEMON,
                            ORTE_RML_PERSISTENT, orte_daemon_recv, NULL);

    /* if the user just wants us to terminate a DVM, then do so */
    if (orte_cmd_options.terminate_dvm) {
        if (ORTE_ERR_OP_IN_PROGRESS != orte_submit_halt()) {
            ORTE_UPDATE_EXIT_STATUS(1);
            goto DONE;
        }
        while (orte_event_base_active) {
           opal_event_loop(orte_event_base, OPAL_EVLOOP_ONCE);
        }
        /* we are terminated when the DVM master shuts down, thereby
         * closing our connection to them. This looks like an error,
         * but is not - so correct our exit status here */
        orte_exit_status = 0;
        goto DONE;
    } else {
        /* spawn the job and its daemons */
        memset(&launchst, 0, sizeof(launchst));
        memset(&completest, 0, sizeof(completest));
        launchst.active = true;
        completest.active = true;
        if (ORTE_SUCCESS != orte_submit_job(argv, NULL,
                                            launched, &launchst,
                                            completed, &completest)) {
            ORTE_UPDATE_EXIT_STATUS(1);
            goto DONE;
        }
    }

    // wait for response and unpack the status, jobid
    while (orte_event_base_active && launchst.active) {
        opal_event_loop(orte_event_base, OPAL_EVLOOP_ONCE);
    }
    ORTE_ACQUIRE_OBJECT(orte_event_base_active);
    if (orte_debug_flag) {
        opal_output(0, "Job %s has launched",
                   (NULL == launchst.jdata) ? "UNKNOWN" : ORTE_JOBID_PRINT(launchst.jdata->jobid));
    }
    if (!orte_event_base_active || ORTE_SUCCESS != launchst.status) {
        goto DONE;
    }

    while (orte_event_base_active && completest.active) {
        opal_event_loop(orte_event_base, OPAL_EVLOOP_ONCE);
    }
    ORTE_ACQUIRE_OBJECT(orte_event_base_active);

 DONE:
    /* cleanup and leave */
    orte_submit_finalize();
    orte_finalize();
    orte_session_dir_cleanup(ORTE_JOBID_WILDCARD);
    /* cleanup the process info */
    orte_proc_info_finalize();

    if (orte_debug_flag) {
        fprintf(stderr, "exiting with status %d\n", orte_exit_status);
    }
    exit(orte_exit_status);
}
