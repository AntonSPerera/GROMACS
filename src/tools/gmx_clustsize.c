/*
 * $Id$
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
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
 * 
 * And Hey:
 * Green Red Orange Magenta Azure Cyan Skyblue
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>
#include <ctype.h>
#include <assert.h>

#include "string2.h"
#include "sysstuff.h"
#include "typedefs.h"
#include "macros.h"
#include "vec.h"
#include "pbc.h"
#include "rmpbc.h"
#include "statutil.h"
#include "xvgr.h"
#include "copyrite.h"
#include "futil.h"
#include "statutil.h"
#include "tpxio.h"
#include "index.h"
#include "smalloc.h"
#include "fftgrid.h"
#include "calcgrid.h"
#include "nrnb.h"
#include "shift_util.h"
#include "pme.h"
#include "gstat.h"
#include "matio.h"

static void clust_size(char *ndx,char *trx,char *xpm,
		       char *xpmw,char *ncl,char *acl, 
		       char *mcl,char *histo,
		       bool bMol,char *tpr,
		       real cut,int nskip,int nlevels,
		       t_rgb rmid,t_rgb rhi)
{
  FILE    *fp,*gp,*hp;
  atom_id *index=NULL;
  int     nindex,natoms,status;
  rvec    *x=NULL,dx;
  matrix  box;
  char    *gname;
  char    timebuf[32];
  bool    bSame;
  /* Topology stuff */
  t_tpxheader tpxh;
  t_topology  top;
  t_block *mols=NULL;
  int     version,generation,sss,ii,jj,aii,ajj,nsame;
  real    ttt,lll;
  /* Cluster size distribution (matrix) */
  real    **cs_dist=NULL;
  real    t,tf,dx2,cut2,*t_x=NULL,*t_y,cmid,cmax,cav;
  int     i,j,k,ai,aj,ak,ci,cj,nframe,nclust,n_x,n_y,max_size=0;
  int     *clust_index,*clust_size,max_clust_size,nav,nhisto;
  t_rgb   rlo = { 1.0, 1.0, 1.0 };
  
  sprintf(timebuf,"Time (%s)",time_unit());
  tf = time_factor();
  fp = xvgropen(ncl,"Number of clusters",timebuf,"N");
  gp = xvgropen(acl,"Average cluster size",timebuf,"#molecules");
  hp = xvgropen(mcl,"Max cluster size",timebuf,"#molecules");
  natoms = read_first_x(&status,trx,&t,&x,box);
  if (bMol) {
    read_tpxheader(tpr,&tpxh,TRUE,&version,&generation);
    if (tpxh.natoms != natoms) 
      fatal_error(0,"tpr and xtc do not match!");
    
    read_tpx(tpr,&sss,&ttt,&lll,NULL,NULL,&natoms,NULL,NULL,NULL,&top);
    mols = &(top.blocks[ebMOLS]);

    /* Make dummy index */
    nindex = mols->nr;
    snew(index,nindex);
    for(i=0; (i<nindex); i++)
      index[i] = i;
    gname = strdup("mols");
    
  }
  else
    rd_index(ndx,1,&nindex,&index,&gname);
  
  snew(clust_index,nindex);
  snew(clust_size,nindex);
  cut2   = cut*cut;
  nframe = 0;
  n_x    = 0;
  snew(t_y,nindex);
  for(i=0; (i<nindex); i++) 
    t_y[i] = i+1;
  do {
    if ((nskip == 0) || ((nskip > 0) && ((nframe % nskip) == 0))) {
      init_pbc(box);
      max_clust_size = 1;
      
      /* Put all atoms/molecules in their own cluster, with size 1 */
      for(i=0; (i<nindex); i++) {
	/* Cluster index is indexed with atom index number */
	clust_index[i] = i;
	/* Cluster size is indexed with cluster number */
	clust_size[i]  = 1;
      }
      
      /* Loop over atoms */
      for(i=0; (i<nindex); i++) {
	ai = index[i];
	ci = clust_index[i];
	
	/* Loop over atoms (only half a matrix) */
	for(j=i+1; (j<nindex); j++) {
	  cj = clust_index[j];
	  
	  /* If they are not in the same cluster already */
	  if (ci != cj) {
	    aj = index[j];
	    
	    /* Compute distance */
	    if (bMol) {
	      bSame = FALSE;
	      for(ii=mols->index[ai]; !bSame && (ii<mols->index[ai+1]); ii++) {
		aii = mols->a[ii];
		for(jj=mols->index[aj]; !bSame && (jj<mols->index[aj+1]); jj++) {
		  ajj   = mols->a[jj];
		  pbc_dx(x[aii],x[ajj],dx);
		  dx2   = iprod(dx,dx);
		  bSame = (dx2 < cut2);
		}
	      }
	    }
	    else {
	      pbc_dx(x[ai],x[aj],dx);
	      dx2 = iprod(dx,dx);
	      bSame = (dx2 < cut2);
	    }
	    /* If distance less than cut-off */
	    if (bSame) {
	      /* Merge clusters: check for all atoms whether they are in 
	       * cluster cj and if so, put them in ci
	       */
	      for(k=0; (k<nindex); k++) {
		if ((clust_index[k] == cj)) {
		  assert(clust_size[cj] > 0);
		  clust_size[cj]--;
		  clust_index[k] = ci;
		  clust_size[ci]++;
		}
	      }
	    }
	  }
	}
      }
      n_x++;
      srenew(t_x,n_x);
      t_x[n_x-1] = t*tf;
      srenew(cs_dist,n_x);
      snew(cs_dist[n_x-1],nindex);
      nclust = 0;
      cav    = 0;
      nav    = 0;
      for(i=0; (i<nindex); i++) {
	ci = clust_size[i];
	if (ci > max_clust_size) 
	  max_clust_size = ci;
	if (ci > 0) {
	  nclust++;
	  cs_dist[n_x-1][ci-1] += 1.0;
	  max_size = max(max_size,ci);
	  if (ci > 1) {
	    cav += ci;
	    nav++;
	  }
	}
      }
      fprintf(fp,"%10.3e  %10d\n",t,nclust);
      if (nav > 0)
	fprintf(gp,"%10.3e  %10.3f\n",t,cav/nav);
      fprintf(hp, "%10.3e  %10d\n", t, max_clust_size);
    }
    nframe++;
  } while (read_next_x(status,&t,natoms,x,box));
  close_trx(status);
  fclose(fp);
  fclose(gp);
  fclose(hp);

  /* Look for the smallest entry that is not zero 
   * This will make that zero is white, and not zero is coloured.
   */
  cmid = 100.0;
  cmax = 0.0;
  for(i=0; (i<n_x); i++)
    for(j=0; (j<max_size); j++) {
      if ((cs_dist[i][j] > 0) && (cs_dist[i][j] < cmid))
	cmid = cs_dist[i][j];
      cmax = max(cs_dist[i][j],cmax);
    }
  fprintf(stderr,"cmid: %g, cmax: %g, max_size: %d\n",cmid,cmax,max_size);
  cmid = 1;
  fp = ffopen(xpm,"w");
  write_xpm3(fp,"Cluster size distribution","# clusters",timebuf,"Size",
	     n_x,max_size,t_x,t_y,cs_dist,0,cmid,cmax,
	     rlo,rmid,rhi,&nlevels);
  fclose(fp);
  cmax = 0.0;
  for(i=0; (i<n_x); i++)
    for(j=0; (j<max_size); j++) {
      cs_dist[i][j] *= (j+1);
      cmax = max(cs_dist[i][j],cmax);
    }
  fprintf(stderr,"cmid: %g, cmax: %g, max_size: %d\n",cmid,cmax,max_size);
  fp = ffopen(xpmw,"w");
  write_xpm3(fp,"Weighted cluster size distribution","Fraction",timebuf,"Size",
	     n_x,max_size,t_x,t_y,cs_dist,0,cmid,cmax,
	     rlo,rmid,rhi,&nlevels);
  fclose(fp);

  fp = xvgropen(histo,"Cluster size distribution","Cluster size","()");
  nhisto = 0;
  fprintf(fp,"%5d  %8.3f\n",0,0.0);
  for(j=0; (j<max_size); j++) {
    real nelem = 0;
    for(i=0; (i<n_x); i++)
      nelem += cs_dist[i][j];
    fprintf(fp,"%5d  %8.3f\n",j+1,nelem/n_x);
    nhisto += (int)((j+1)*nelem/n_x);
  }
  fprintf(fp,"%5d  %8.3f\n",j+1,0.0);
  fclose(fp);

  fprintf(stderr,"Total number of atoms in clusters =  %d\n",nhisto);
  
  sfree(clust_index);
  sfree(clust_size);
  sfree(index);
}

int gmx_clustsize(int argc,char *argv[])
{
  static char *desc[] = {
    "This program computes the size distributions of molecular/atomic clusters in",
    "the gas phase. The output is given in the form of a XPM file.",
    "The total number of clusters is written to a XVG file.[PAR]",
    "When the [TT]-mol[tt] option is given clusters will be made out of",
    "molecules rather than atoms, which allows clustering of large molecules.",
    "In this case an index file would still contain atom numbers",
    "or your calculcation will die with a SEGV."
  };
  static real cutoff   = 0.35;
  static int  nskip    = 0;
  static int  nlevels  = 20;
  static bool bMol     = FALSE;
  static rvec rlo      = { 1.0, 1.0, 0.0 };
  static rvec rhi      = { 0.0, 0.0, 1.0 };
  t_pargs pa[] = {
    { "-cut",      FALSE, etREAL, {&cutoff},
      "Largest distance (nm) to be considered in a cluster" },
    { "-mol",      FALSE, etBOOL, {&bMol},
      "Cluster molecules rather than atoms (needs tpr file)" },
    { "-nskip",    FALSE, etINT,  {&nskip},
      "Number of frames to skip between writing" },
    { "-nlevels",  FALSE, etINT,  {&nlevels},
      "Number of levels of grey in xpm output" },
    { "-rgblo",    FALSE, etRVEC, {rlo},
      "RGB values for the color of the lowest occupied cluster size" },
    { "-rgbhi",    FALSE, etRVEC, {rhi},
      "RGB values for the color of the highest occupied cluster size" }
  };
#define NPA asize(pa)
  char       *fnTPS,*fnNDX;
  bool       bSQ,bRDF;
  t_rgb      rgblo,rgbhi;
  
  t_filenm   fnm[] = {
    { efTRX, "-f",  NULL,         ffREAD  },
    { efTPR, NULL,  NULL,         ffOPTRD },
    { efNDX, NULL,  NULL,         ffOPTRD },
    { efXPM, "-o", "csize",       ffWRITE },
    { efXPM, "-ow","csizew",      ffWRITE },
    { efXVG, "-nc","nclust",      ffWRITE },
    { efXVG, "-mc","maxclust",    ffWRITE },
    { efXVG, "-ac","avclust",     ffWRITE },
    { efXVG, "-hc","histo-clust", ffWRITE }
  };
#define NFILE asize(fnm)
  
  CopyRight(stderr,argv[0]);
  parse_common_args(&argc,argv,PCA_CAN_VIEW | PCA_CAN_TIME | PCA_TIME_UNIT | PCA_BE_NICE,
		    NFILE,fnm,NPA,pa,asize(desc),desc,0,NULL);

  fnNDX = ftp2fn_null(efNDX,NFILE,fnm);
  rgblo.r = rlo[XX],rgblo.g = rlo[YY],rgblo.b = rlo[ZZ];
  rgbhi.r = rhi[XX],rgbhi.g = rhi[YY],rgbhi.b = rhi[ZZ];
    
  clust_size(fnNDX,ftp2fn(efTRX,NFILE,fnm),opt2fn("-o",NFILE,fnm),
	     opt2fn("-ow",NFILE,fnm),
	     opt2fn("-nc",NFILE,fnm),opt2fn("-ac",NFILE,fnm),
	     opt2fn("-mc",NFILE,fnm),opt2fn("-hc",NFILE,fnm),
	     bMol,ftp2fn(efTPR,NFILE,fnm),
	     cutoff,nskip,nlevels,rgblo,rgbhi);

  thanx(stderr);
  
  return 0;
}