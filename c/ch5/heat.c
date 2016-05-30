static char help[] =
"Solves time-dependent heat equation in 2D using TS.  Option prefix -heat_.\n"
"Equation is  u_t = D laplacian u + f.  Domain is (0,1) x (0,1).\n"
"Boundary conditions are non-homogeneous Neumann in x and periodic in y.\n"
"Energy is conserved (with default choices).  Discretization by\n"
"centered finite differences.  Converts the PDE into a system  X_t = G(t,X)\n"
"(PETSc type 'nonlinear') and uses theta method time-stepping by default.\n";

// $ ./heat -help |grep heat_
// $ ./heat -help |grep ts_type
// $ ./heat -snes_type test -snes_test_display // result suggests jacobian is correct

//run-time info
// $ ./heat -ts_view
// $ ./heat -dm_view draw -draw_pause 2 -da_refine 1  // shows BOUNDARY_NONE and BOUNDARY_PERIODIC clearly
// $ ./heat -ts_monitor_solution draw   // note -ts_monitor needed
// $ ./heat -ts_monitor -heat_monitor -snes_converged_reason

//good (use default BEULER):
// $ mpiexec -n 4 ./heat -ts_monitor -heat_monitor -snes_converged_reason -ts_monitor_solution draw -snes_monitor -da_refine 7

//explodes (as it should):
// $ ./heat -ts_monitor -ts_monitor_solution draw -da_refine 7 -ts_type rk -ts_rk_type 1fe -ts_adapt_type none

//agonizingly slow (RK is adapting):
// $ ./heat -ts_monitor -ts_monitor_solution draw -da_refine 4 -ts_type rk

//wobbles (typical Crank-Nicolson):
// $ ./heat -ts_monitor -ts_monitor_solution draw -da_refine 4 -ts_type cn

//theta methods:
// $ ./heat -help |grep ts_theta
// $ ./heat -ts_type theta -ts_theta_theta 1                        // = BEuler
// $ ./heat -ts_type theta -ts_theta_theta 0.5 -ts_theta_endpoint   // = Crank-Nicolson

//wobbles mostly fixed:
// $ ./heat -ts_monitor -ts_monitor_solution draw -da_refine 4 -ts_type theta -ts_theta_theta 0.7 -ts_theta_endpoint

//good adaptive:
// $ ./heat -ts_monitor -ts_monitor_solution draw -da_refine 4 -ts_type gl

#include <petsc.h>

//HEATCTX
typedef struct {
  DM     da;
  Vec    f,    // source f(x,y)
         gamma;// boundary condition; = gamma_0(y) on left boundary
               //                     = gamma_1(y) on right boundary
  double D;    // conductivity
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


//MONITOR
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
    ierr = PetscPrintf(PETSC_COMM_WORLD,"  energy = %g\n",energy); CHKERRQ(ierr);
    return 0;
}
//ENDMONITOR


PetscErrorCode SetSourceF(Vec f, HeatCtx* user) {
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

    ierr = VecSet(gamma,NAN); CHKERRQ(ierr);  // start by invalidating
    ierr = DMDAGetLocalInfo(user->da,&info); CHKERRQ(ierr);
    ierr = Spacings(user->da,NULL,&hy); CHKERRQ(ierr);
    ierr = DMDAVecGetArray(user->da,gamma,&agamma); CHKERRQ(ierr);
    for (j = info.ys; j < info.ys+info.ym; j++) {
        y = hy * j;
        if (info.xs == 0)
            agamma[j][0] = sin(6.0 * PETSC_PI * y);
        if (info.xs+info.xm == info.mx)
            agamma[j][info.mx-1] = 0.0;
    }
    ierr = DMDAVecRestoreArray(user->da,gamma,&agamma); CHKERRQ(ierr);
    return 0;
}


//RHSFUNCTION
PetscErrorCode FormRHSFunctionLocal(DMDALocalInfo *info, double t, double **au,
                                    double **aG, HeatCtx *user) {
  PetscErrorCode ierr;
  int      i, j;
  double   hx, hy, uleft, uright, uxx, uyy, **af, **agamma;

  ierr = Spacings(info->da,&hx,&hy); CHKERRQ(ierr);
  ierr = DMDAVecGetArray(user->da,user->f,&af); CHKERRQ(ierr);
  ierr = DMDAVecGetArray(user->da,user->gamma,&agamma); CHKERRQ(ierr);
  for (j = info->ys; j < info->ys + info->ym; j++) {
      for (i = info->xs; i < info->xs + info->xm; i++) {
          if (i == 0)
              uleft = au[j][i+1] - 2.0 * hx * agamma[j][i];
          else
              uleft = au[j][i-1];
          if (i == info->mx-1)
              uright = au[j][i-1] - 2.0 * hx * agamma[j][i];
          else
              uright = au[j][i+1];
          uxx = (uleft - 2.0 * au[j][i]+ uright) / (hx*hx);
          uyy = (au[j-1][i] - 2.0 * au[j][i]+ au[j+1][i]) / (hy*hy);
          aG[j][i] = user->D * (uxx + uyy) + af[j][i];
      }
  }
  ierr = DMDAVecRestoreArray(user->da,user->f,&af); CHKERRQ(ierr);
  ierr = DMDAVecRestoreArray(user->da,user->gamma,&agamma); CHKERRQ(ierr);
  return 0;
}
//ENDRHSFUNCTION


//RHSJACOBIAN
PetscErrorCode FormRHSJacobianLocal(DMDALocalInfo *info, double t, double **au,
                                    Mat J, Mat P, HeatCtx *user) {
    PetscErrorCode ierr;
    int            i, j, ncols;
    const double   D = user->D;
    double         hx, hy, hx2, hy2, v[5];
    MatStencil     col[5],row;

    ierr = Spacings(info->da,&hx,&hy); CHKERRQ(ierr);
    hx2 = hx * hx;  hy2 = hy * hy;
    for (j = info->ys; j < info->ys+info->ym; j++) {
        row.j = j;  col[0].j = j;
        for (i = info->xs; i < info->xs+info->xm; i++) {
            ncols = 5;
            row.i = i;
            col[0].i = i;
            v[0] = - 2.0 * D * (1.0 / hx2 + 1.0 / hy2);
            col[1].j = j-1;  col[1].i = i;    v[1] = D / hy2;
            col[2].j = j+1;  col[2].i = i;    v[2] = D / hy2;
            col[3].j = j;    col[3].i = i-1;  v[3] = D / hx2;
            col[4].j = j;    col[4].i = i+1;  v[4] = D / hx2;
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
  Vec            u, uexact;
  DMDALocalInfo  info;
  double         hx, hy, hxhy, t0, dt, tf;
  PetscBool      monitorenergy = PETSC_FALSE;

  PetscInitialize(&argc,&argv,(char*)0,help);

  user.D  = 1.0;
  ierr = PetscOptionsBegin(PETSC_COMM_WORLD, "heat_", "options for heat", ""); CHKERRQ(ierr);
  ierr = PetscOptionsReal("-D","thermal diffusivity",
           "heat.c",user.D,&user.D,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsBool("-monitor","also display total heat energy at each step",
           "heat.c",monitorenergy,&monitorenergy,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  ierr = DMDACreate2d(PETSC_COMM_WORLD,
                      DM_BOUNDARY_NONE, DM_BOUNDARY_PERIODIC,
                      DMDA_STENCIL_STAR,
                      -4,-3,PETSC_DECIDE,PETSC_DECIDE,
                      1,  // degrees of freedom
                      1,  // stencil width
                      NULL,NULL,&user.da); CHKERRQ(ierr);
  ierr = DMDASetUniformCoordinates(user.da, 0.0, 1.0, 0.0, 1.0, -1.0, -1.0); CHKERRQ(ierr);
  ierr = DMSetApplicationContext(user.da,&user); CHKERRQ(ierr);

  ierr = DMCreateGlobalVector(user.da,&u); CHKERRQ(ierr);
  ierr = VecDuplicate(u,&uexact); CHKERRQ(ierr);
  ierr = VecDuplicate(u,&(user.f)); CHKERRQ(ierr);
  ierr = VecDuplicate(u,&(user.gamma)); CHKERRQ(ierr);
  ierr = SetSourceF(user.f,&user); CHKERRQ(ierr);
  ierr = SetNeumannValues(user.gamma,&user); CHKERRQ(ierr);

//TSSETUP
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
//ENDTSSETUP

  // report on set up
  ierr = TSGetTime(ts,&t0); CHKERRQ(ierr);
  ierr = TSGetTimeStep(ts,&dt); CHKERRQ(ierr);
  ierr = DMDAGetLocalInfo(user.da,&info); CHKERRQ(ierr);
  ierr = Spacings(user.da,&hx,&hy); CHKERRQ(ierr);
  hxhy = PetscMin(hx,hy);  hxhy = hxhy * hxhy;
  ierr = PetscPrintf(PETSC_COMM_WORLD,
           "solving on %d x %d grid from t0=%g with initial step dt=%g ...\n"
           "(initial step stability ratio:  D dt / (min{dx,dy}^2) = %g)\n",
           info.mx,info.my,t0,dt,user.D*dt/hxhy); CHKERRQ(ierr);

  ierr = VecSet(u,0.0); CHKERRQ(ierr);
  ierr = TSSolve(ts,u); CHKERRQ(ierr);

  ierr = TSGetTime(ts,&tf); CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
           "... done ... final time tf=%g\n",tf); CHKERRQ(ierr);

  VecDestroy(&u);  VecDestroy(&uexact);
  VecDestroy(&(user.f));  VecDestroy(&(user.gamma));
  TSDestroy(&ts);  DMDestroy(&user.da);
  PetscFinalize();
  return 0;
}
