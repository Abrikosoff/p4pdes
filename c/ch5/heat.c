static char help[] =
"Solves time-dependent heat equation in 2D using TS.  Option prefix -heat_.\n"
"Equation is  u_t = D_0 laplacian u + f.  Domain is (0,1) x (0,1).\n"
"Boundary conditions are non-homogeneous Neumann in x and periodic in y.\n"
"Energy is conserved (for these particular conditions/source) and an extra\n"
"'monitor' is demonstrated.  Discretization is by centered finite differences.\n"
"Converts the PDE into a system  X_t = G(t,X) (PETSc type 'nonlinear') by\n"
"method of lines.  Uses backward Euler time-stepping by default.\n";

#include <petsc.h>

//STARTHEATCTX
typedef struct {
  DM     da;
  double D0;    // conductivity
  Vec    f,     // source f(x,y)
         gamma; // Neumann boundary condition; = gamma(y) on left boundary
} HeatCtx;
//ENDHEATCTX

PetscErrorCode Spacings(DM da, double *hx, double *hy) {
    PetscErrorCode ierr;
    DMDALocalInfo  info;
    ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);
    if (hx)  *hx = 1.0 / (double)(info.mx-1);
    if (hy)  *hy = 1.0 / (double)(info.my);   // periodic direction
    return 0;
}

//STARTMONITOR
PetscErrorCode EnergyMonitor(TS ts, PetscInt step, PetscReal time, Vec u,
                             void *ctx) {
    PetscErrorCode ierr;
    HeatCtx        *user = (HeatCtx*)ctx;
    double         lenergy = 0.0, energy, hx, hy, **au;
    int            i,j;
    MPI_Comm       com;
    DMDALocalInfo  info;

    ierr = DMDAGetLocalInfo(user->da,&info); CHKERRQ(ierr);
    ierr = DMDAVecGetArrayRead(user->da,u,&au); CHKERRQ(ierr);
    for (j = info.ys; j < info.ys + info.ym; j++) {
        for (i = info.xs; i < info.xs + info.xm; i++) {
            if ((i == 0) || (i == info.mx-1))
                lenergy += 0.5 * au[j][i];
            else
                lenergy += au[j][i];
        }
    }
    ierr = DMDAVecRestoreArrayRead(user->da,u,&au); CHKERRQ(ierr);
    ierr = Spacings(user->da,&hx,&hy); CHKERRQ(ierr);
    lenergy *= hx * hy;
    ierr = PetscObjectGetComm((PetscObject)(user->da),&com); CHKERRQ(ierr);
    ierr = MPI_Allreduce(&lenergy,&energy,1,MPI_DOUBLE,MPI_SUM,com); CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD,"  energy = %.2e\n",energy); CHKERRQ(ierr);
    return 0;
}
//ENDMONITOR

PetscErrorCode SetSource(Vec f, HeatCtx* user) {
    PetscErrorCode ierr;
    DMDALocalInfo  info;
    int            i,j;
    double         hx, hy, **af, x, y, q;

    ierr = DMDAGetLocalInfo(user->da,&info); CHKERRQ(ierr);
    ierr = Spacings(user->da,&hx,&hy); CHKERRQ(ierr);
    ierr = DMDAVecGetArray(user->da,f,&af); CHKERRQ(ierr);
    for (j = info.ys; j < info.ys+info.ym; j++) {
        y = hy * j;
        for (i = info.xs; i < info.xs+info.xm; i++) {
            x = hx * i;
            q = (x-0.6) * (x-0.6);
            af[j][i] = 3.0 * exp(-25.0*q) * sin(2.0*PETSC_PI*y);
        }
    }
    ierr = DMDAVecRestoreArray(user->da,f,&af); CHKERRQ(ierr);
    return 0;
}

PetscErrorCode SetNeumannValues(Vec gamma, HeatCtx* user) {
    PetscErrorCode ierr;
    DMDALocalInfo  info;
    int            j;
    double         hy, **agamma, y;

    ierr = VecSet(gamma,NAN); CHKERRQ(ierr);  // invalidate
    ierr = DMDAGetLocalInfo(user->da,&info); CHKERRQ(ierr);
    if (info.xs == 0) { // only a process that owns i=0 will do the following
        ierr = Spacings(user->da,NULL,&hy); CHKERRQ(ierr);
        ierr = DMDAVecGetArray(user->da,gamma,&agamma); CHKERRQ(ierr);
        for (j = info.ys; j < info.ys+info.ym; j++) {
            y = hy * j;
            agamma[j][0] = sin(6.0 * PETSC_PI * y);
        }
        ierr = DMDAVecRestoreArray(user->da,gamma,&agamma); CHKERRQ(ierr);
    }
    return 0;
}

//STARTRHSFUNCTION
PetscErrorCode FormRHSFunctionLocal(DMDALocalInfo *info, double t, double **au,
                                    double **aG, HeatCtx *user) {
  PetscErrorCode ierr;
  int      i, j, mx = info->mx;
  double   hx, hy, ul, ur, uxx, uyy, **af, **agamma;

  ierr = Spacings(info->da,&hx,&hy); CHKERRQ(ierr);
  ierr = DMDAVecGetArray(user->da,user->f,&af); CHKERRQ(ierr);
  ierr = DMDAVecGetArray(user->da,user->gamma,&agamma); CHKERRQ(ierr);
  for (j = info->ys; j < info->ys + info->ym; j++) {
      for (i = info->xs; i < info->xs + info->xm; i++) {
          // apply Neumann b.c. (if needed) to determine left/right neighbor
          ul = (i == 0)    ? au[j][i+1] + 2.0 * hx * agamma[j][i] : au[j][i-1];
          ur = (i == mx-1) ? au[j][i-1]                           : au[j][i+1];
          uxx = (ul - 2.0 * au[j][i]+ ur) / (hx*hx);
          // j-1, j+1 values always valid because DMDA is periodic in y
          uyy = (au[j-1][i] - 2.0 * au[j][i]+ au[j+1][i]) / (hy*hy);
          aG[j][i] = user->D0 * (uxx + uyy) + af[j][i];
      }
  }
  ierr = DMDAVecRestoreArray(user->da,user->f,&af); CHKERRQ(ierr);
  ierr = DMDAVecRestoreArray(user->da,user->gamma,&agamma); CHKERRQ(ierr);
  return 0;
}
//ENDRHSFUNCTION

//STARTRHSJACOBIAN
PetscErrorCode FormRHSJacobianLocal(DMDALocalInfo *info, double t, double **au,
                                    Mat J, Mat P, HeatCtx *user) {
    PetscErrorCode ierr;
    int            i, j, ncols;
    const double   D = user->D0;
    double         hx, hy, hx2, hy2, v[5];
    MatStencil     col[5],row;

    ierr = Spacings(info->da,&hx,&hy); CHKERRQ(ierr);
    hx2 = hx * hx;  hy2 = hy * hy;
    for (j = info->ys; j < info->ys+info->ym; j++) {
        row.j = j;  col[0].j = j;
        for (i = info->xs; i < info->xs+info->xm; i++) {
            // set up a standard 5-point stencil for the row
            row.i = i;
            col[0].i = i;
            v[0] = - 2.0 * D * (1.0 / hx2 + 1.0 / hy2);
            col[1].j = j-1;  col[1].i = i;    v[1] = D / hy2;
            col[2].j = j+1;  col[2].i = i;    v[2] = D / hy2;
            col[3].j = j;    col[3].i = i-1;  v[3] = D / hx2;
            col[4].j = j;    col[4].i = i+1;  v[4] = D / hx2;
            ncols = 5;
            // if at the boundary, edit the row back to 4 nonzeros
            if (i == 0) {
                ncols = 4;
                col[3].j = j;  col[3].i = i+1;  v[3] = 2.0 * D / hx2;
            } else if (i == info->mx-1) {
                ncols = 4;
                col[3].j = j;  col[3].i = i-1;  v[3] = 2.0 * D / hx2;
            }
            ierr = MatSetValuesStencil(P,1,&row,ncols,col,v,INSERT_VALUES); CHKERRQ(ierr);
        }
    }

    ierr = MatAssemblyBegin(P,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    ierr = MatAssemblyEnd(P,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    if (J != P) {
        ierr = MatAssemblyBegin(J,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
        ierr = MatAssemblyEnd(J,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    }
    return 0;
}
//ENDRHSJACOBIAN

int main(int argc,char **argv)
{
  PetscErrorCode ierr;
  HeatCtx        user;
  TS             ts;
  Vec            u;
  DMDALocalInfo  info;
  double         hx, hy, hxhy, t0, dt;
  PetscBool      monitorenergy = PETSC_FALSE;

  PetscInitialize(&argc,&argv,(char*)0,help);

  user.D0  = 1.0;
  ierr = PetscOptionsBegin(PETSC_COMM_WORLD, "heat_", "options for heat", ""); CHKERRQ(ierr);
  ierr = PetscOptionsReal("-D0","constant thermal diffusivity",
           "heat.c",user.D0,&user.D0,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsBool("-monitor","also display total heat energy at each step",
           "heat.c",monitorenergy,&monitorenergy,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

//STARTDMDASETUP
  ierr = DMDACreate2d(PETSC_COMM_WORLD,
               DM_BOUNDARY_NONE, DM_BOUNDARY_PERIODIC, DMDA_STENCIL_STAR,
               5,4,PETSC_DECIDE,PETSC_DECIDE,  // default to hx=hx=0.25 grid
               1,1,                       // degrees of freedom, stencil width
               NULL,NULL,&user.da); CHKERRQ(ierr);
  ierr = DMSetFromOptions(user.da); CHKERRQ(ierr);
  ierr = DMSetUp(user.da); CHKERRQ(ierr);
  ierr = DMSetApplicationContext(user.da,&user); CHKERRQ(ierr);
  ierr = DMCreateGlobalVector(user.da,&u); CHKERRQ(ierr);
  ierr = VecDuplicate(u,&(user.f)); CHKERRQ(ierr);
  ierr = VecDuplicate(u,&(user.gamma)); CHKERRQ(ierr);
  ierr = SetSource(user.f,&user); CHKERRQ(ierr);
  ierr = SetNeumannValues(user.gamma,&user); CHKERRQ(ierr);
//ENDDMDASETUP

//STARTTSSETUP
  ierr = TSCreate(PETSC_COMM_WORLD,&ts); CHKERRQ(ierr);
  ierr = TSSetProblemType(ts,TS_NONLINEAR); CHKERRQ(ierr);
  ierr = TSSetDM(ts,user.da); CHKERRQ(ierr);
  ierr = DMDATSSetRHSFunctionLocal(user.da,INSERT_VALUES,
           (DMDATSRHSFunctionLocal)FormRHSFunctionLocal,&user); CHKERRQ(ierr);
  ierr = DMDATSSetRHSJacobianLocal(user.da,
           (DMDATSRHSJacobianLocal)FormRHSJacobianLocal,&user); CHKERRQ(ierr);
  if (monitorenergy) {
      ierr = TSMonitorSet(ts,EnergyMonitor,&user,NULL); CHKERRQ(ierr);
  }
  ierr = TSSetType(ts,TSBEULER); CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(ts,TS_EXACTFINALTIME_MATCHSTEP); CHKERRQ(ierr);
  ierr = TSSetInitialTimeStep(ts,0.0,0.01); CHKERRQ(ierr);
  ierr = TSSetDuration(ts,1000000,0.1); CHKERRQ(ierr);
  ierr = TSSetFromOptions(ts);CHKERRQ(ierr);

  // report on set up //STRIP
  ierr = TSGetTime(ts,&t0); CHKERRQ(ierr); //STRIP
  ierr = TSGetTimeStep(ts,&dt); CHKERRQ(ierr); //STRIP
  ierr = DMDAGetLocalInfo(user.da,&info); CHKERRQ(ierr); //STRIP
  ierr = Spacings(user.da,&hx,&hy); CHKERRQ(ierr); //STRIP
  hxhy = PetscMin(hx,hy);  hxhy = hxhy * hxhy; //STRIP
  ierr = PetscPrintf(PETSC_COMM_WORLD, //STRIP
           "solving on %d x %d grid with dx=%g x dy=%g cells, t0=%g,\n" //STRIP
           "and initial step dt=%g (so D0 dt / (dx dy) = %g) ...\n", //STRIP
           info.mx,info.my,hx,hy, //STRIP
           t0,dt,user.D0*dt/hxhy); CHKERRQ(ierr); //STRIP
  // solve //STRIP
  ierr = VecSet(u,0.0); CHKERRQ(ierr);   // initial condition
  ierr = TSSolve(ts,u); CHKERRQ(ierr);
//ENDTSSETUP

  VecDestroy(&u);  VecDestroy(&(user.f));  VecDestroy(&(user.gamma));
  TSDestroy(&ts);  DMDestroy(&user.da);
  PetscFinalize();
  return 0;
}

