/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2009      Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2015      Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2019-2020 Intel, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "prrte_config.h"

#include "src/util/output.h"
#include "src/mca/mca.h"
#include "src/mca/base/base.h"
#include "src/mca/base/prrte_mca_base_component_repository.h"
#include "constants.h"

extern int prrte_mca_base_opened;

/*
 * Main MCA shutdown.
 */

void prrte_mca_base_close (void)
{
    assert (prrte_mca_base_opened);
    if (--prrte_mca_base_opened) {
        return;
    }

    /* deregister all MCA base parameters */
    int group_id = prrte_mca_base_var_group_find ("prrte", "mca", "base");

    if (-1 < group_id) {
        prrte_mca_base_var_group_deregister (group_id);
    }

    /* release the default paths */
    if (NULL != prrte_mca_base_system_default_path) {
        free (prrte_mca_base_system_default_path);
    }
    prrte_mca_base_system_default_path = NULL;

    if (NULL != prrte_mca_base_user_default_path) {
        free (prrte_mca_base_user_default_path);
    }
    prrte_mca_base_user_default_path = NULL;

    /* Close down the component repository */
    prrte_mca_base_component_repository_finalize();

    /* Shut down the dynamic component finder */
    prrte_mca_base_component_find_finalize();

    /* Close prrte output stream 0 */
    prrte_output_close(0);
}
