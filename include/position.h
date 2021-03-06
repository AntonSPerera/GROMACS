/*
 *
 *                This source code is part of
 *
 *                 G   R   O   M   A   C   S
 *
 *          GROningen MAchine for Chemical Simulations
 *
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2009, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 *
 * For more info, check our website at http://www.gromacs.org
 */
/*! \file
 * \brief API for handling positions.
 */
#ifndef POSITION_H
#define POSITION_H

#include <typedefs.h>

#include <indexutil.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*! \brief
 * Stores a set of positions together with their origins.
 */
typedef struct gmx_ana_pos_t
{
    /*! \brief
     * Number of positions.
     */
    int                 nr;
    /*! \brief
     * Array of positions.
     */
    rvec               *x;
    /*! \brief
     * Mapping of the current positions to the original group.
     *
     * \see gmx_ana_indexmap_t
     */
    gmx_ana_indexmap_t  m;
    /*! \brief
     * Pointer to the current evaluation group.
     */
    gmx_ana_index_t    *g;
} gmx_ana_pos_t;

//! Initializes an empty position structure.
extern void
gmx_ana_pos_clear(gmx_ana_pos_t *pos);
//! Ensures that enough memory has been allocated to store positions.
extern void
gmx_ana_pos_reserve(gmx_ana_pos_t *pos, int n, int isize);
//! Initializes a \c gmx_ana_pos_t to represent a constant position.
extern void
gmx_ana_pos_init_const(gmx_ana_pos_t *pos, rvec x);
//! Frees the memory allocated for position storage.
extern void
gmx_ana_pos_deinit(gmx_ana_pos_t *pos);
//! Frees the memory allocated for positions.
extern void
gmx_ana_pos_free(gmx_ana_pos_t *pos);
//! Copies the evaluated positions to a preallocated data structure.
extern void
gmx_ana_pos_copy(gmx_ana_pos_t *dest, gmx_ana_pos_t *src, bool bFirst);

#ifdef __cplusplus
}
#endif

#endif
