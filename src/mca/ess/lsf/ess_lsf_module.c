/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007-2011 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2013-2020 Intel, Inc.  All rights reserved.
 * Copyright (c) 2016-2019 Research Organization for Information Science
 *                         and Technology (RIST).  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

#include "prrte_config.h"
#include "constants.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif  /* HAVE_UNISTD_H */
#include <string.h>
#include <ctype.h>

#include <lsf/lsbatch.h>

#include "src/util/prrte_environ.h"
#include "src/util/argv.h"

#include "src/util/show_help.h"
#include "src/util/name_fns.h"
#include "src/util/proc_info.h"
#include "src/runtime/prrte_globals.h"
#include "src/mca/errmgr/errmgr.h"
#include "src/pmix/pmix-internal.h"

#include "src/mca/ess/ess.h"
#include "src/mca/ess/base/base.h"
#include "src/mca/ess/lsf/ess_lsf.h"

static int lsf_set_name(void);

static int rte_init(int argc, char **argv);
static int rte_finalize(void);

prrte_ess_base_module_t prrte_ess_lsf_module = {
    rte_init,
    rte_finalize,
    NULL,
    NULL /* ft_event */
};

/*
 * Local variables
 */
static prrte_node_rank_t my_node_rank=PRRTE_NODE_RANK_INVALID;


static int rte_init(int argc, char **argv)
{
    int ret;
    char *error = NULL;

    /* run the prolog */
    if (PRRTE_SUCCESS != (ret = prrte_ess_base_std_prolog())) {
        error = "prrte_ess_base_std_prolog";
        goto error;
    }

    /* Start by getting a unique name */
    lsf_set_name();

    if (PRRTE_SUCCESS != (ret = prrte_ess_base_prted_setup())) {
        PRRTE_ERROR_LOG(ret);
        error = "prrte_ess_base_prted_setup";
        goto error;
    }
    return PRRTE_SUCCESS;


error:
    if (PRRTE_ERR_SILENT != ret && !prrte_report_silent_errors) {
        prrte_show_help("help-prrte-runtime.txt",
                       "prrte_init:startup:internal-failure",
                       true, error, PRRTE_ERROR_NAME(ret), ret);
    }

    return ret;
}

static int rte_finalize(void)
{
    int ret;

    if (PRRTE_SUCCESS != (ret = prrte_ess_base_prted_finalize())) {
        PRRTE_ERROR_LOG(ret);
    }

    return ret;;
}

static int lsf_set_name(void)
{
    int rc;
    int lsf_nodeid;
    prrte_jobid_t jobid;
    prrte_vpid_t vpid;

    if (NULL == prrte_ess_base_nspace) {
        PRRTE_ERROR_LOG(PRRTE_ERR_NOT_FOUND);
        return PRRTE_ERR_NOT_FOUND;
    }

    PRRTE_PMIX_CONVERT_NSPACE(rc, &jobid, prrte_ess_base_nspace);
    if (PRRTE_SUCCESS != rc) {
        return rc;
    }
    PMIX_LOAD_NSPACE(prrte_process_info.myproc.nspace, prrte_ess_base_nspace);
    PRRTE_PROC_MY_NAME->jobid = jobid;

    if (NULL == prrte_ess_base_vpid) {
        PRRTE_ERROR_LOG(PRRTE_ERR_NOT_FOUND);
        return PRRTE_ERR_NOT_FOUND;
    }
    vpid = strtoul(prrte_ess_base_vpid, NULL, 10);

    lsf_nodeid = atoi(getenv("LSF_PM_TASKID"));
    prrte_output_verbose(1, prrte_ess_base_framework.framework_output,
                        "ess:lsf found LSF_PM_TASKID set to %d",
                        lsf_nodeid);
    PRRTE_PROC_MY_NAME->vpid = vpid + lsf_nodeid - 1;
    prrte_process_info.myproc.rank = PRRTE_PROC_MY_NAME->vpid;

    /* get the num procs as provided in the cmd line param */
    prrte_process_info.num_daemons = prrte_ess_base_num_procs;


    return PRRTE_SUCCESS;
}
