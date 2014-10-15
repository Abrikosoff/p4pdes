#include <petscmat.h>
#include <petscdmda.h>

//CREATEMATRIX
PetscErrorCode formdirichletlaplacian(DM da, Mat A) {
  PetscErrorCode ierr;
  DMDALocalInfo  info;
  ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);

  PetscInt   i, j;
  PetscReal  hx = 1./(double)(info.mx-1),
             hy = 1./(double)(info.my-1);  // domain is [0,1] x [0,1]
  for (j=info.ys; j<info.ys+info.ym; j++) {
    for (i=info.xs; i<info.xs+info.xm; i++) {
      MatStencil  row, col[5];
      PetscReal   v[5];
      PetscInt    ncols = 0;
      row.j = j;               // the row of A corresponding to the unknown at
      row.i = i;               //     coordinates (x_i,y_j)
      col[ncols].j = j;        // in that diagonal entry ...
      col[ncols].i = i;
      if ( (i==0) || (i==info.mx-1) || (j==0) || (j==info.my-1) ) { // ... on bdry
        v[ncols++] = 1.0;      //     ... we have "1 * u = 0"
      } else {                 // while everywhere else ...
        v[ncols++] = 2*(hy/hx + hx/hy); // ... we put 4 (if hx = hy)
        // if neighbor is NOT known to be zero, we put -1 (hx = hy case) in the
        //     column corresponding to neighbor
        if (i-1>0) {
          col[ncols].j = j;    col[ncols].i = i-1;  v[ncols++] = -hy/hx;  }
        if (i+1<info.mx-1) {
          col[ncols].j = j;    col[ncols].i = i+1;  v[ncols++] = -hy/hx;  }
        if (j-1>0) {
          col[ncols].j = j-1;  col[ncols].i = i;    v[ncols++] = -hx/hy;  }
        if (j+1<info.my-1) {
          col[ncols].j = j+1;  col[ncols].i = i;    v[ncols++] = -hx/hy;  }
      }
      ierr = MatSetValuesStencil(A,1,&row,ncols,col,v,INSERT_VALUES); CHKERRQ(ierr);
    }
  }

  ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
  return 0;
}
//ENDCREATEMATRIX

