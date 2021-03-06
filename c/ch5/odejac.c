static char help[] =
"ODE system solver example using TS, with Jacobian.  Sets TS type to\n"
"implicit Crank-Nicolson.  Compare ode.c.\n\n";

#include <petsc.h>

extern PetscErrorCode SetFromExact(double, Vec);
extern PetscErrorCode FormRHSFunction(TS, double, Vec, Vec, void*);
extern PetscErrorCode FormRHSJacobian(TS, double, Vec, Mat, Mat, void*);

int main(int argc,char **argv) {
  PetscErrorCode ierr;
  const int N = 2;
  int       steps;
  double    t0 = 0.0, tf = 20.0, dt = 0.1, err;
  Vec       y, yexact;
  Mat       J;
  TS        ts;

  PetscInitialize(&argc,&argv,(char*)0,help);

  ierr = VecCreate(PETSC_COMM_WORLD,&y); CHKERRQ(ierr);
  ierr = VecSetSizes(y,PETSC_DECIDE,N); CHKERRQ(ierr);
  ierr = VecSetFromOptions(y); CHKERRQ(ierr);
  ierr = VecDuplicate(y,&yexact); CHKERRQ(ierr);

  ierr = TSCreate(PETSC_COMM_WORLD,&ts); CHKERRQ(ierr);
  ierr = TSSetProblemType(ts,TS_NONLINEAR); CHKERRQ(ierr);
  ierr = TSSetRHSFunction(ts,NULL,FormRHSFunction,NULL); CHKERRQ(ierr);

//STARTMATJ
  ierr = MatCreate(PETSC_COMM_WORLD,&J); CHKERRQ(ierr);
  ierr = MatSetSizes(J,PETSC_DECIDE,PETSC_DECIDE,N,N); CHKERRQ(ierr);
  ierr = MatSetFromOptions(J); CHKERRQ(ierr);
  ierr = MatSetUp(J); CHKERRQ(ierr);
  ierr = TSSetRHSJacobian(ts,J,J,FormRHSJacobian,NULL); CHKERRQ(ierr);
  ierr = TSSetType(ts,TSCN); CHKERRQ(ierr);
//ENDMATJ

  // set time axis
  ierr = TSSetTime(ts,t0); CHKERRQ(ierr);
  ierr = TSSetMaxTime(ts,tf); CHKERRQ(ierr);
  ierr = TSSetTimeStep(ts,dt); CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(ts,TS_EXACTFINALTIME_MATCHSTEP); CHKERRQ(ierr);
  ierr = TSSetFromOptions(ts); CHKERRQ(ierr);  // can override defaults

  // set initial value and solve
  ierr = TSGetTime(ts,&t0); CHKERRQ(ierr);
  ierr = SetFromExact(t0,y); CHKERRQ(ierr);
  ierr = TSSolve(ts,y); CHKERRQ(ierr);

  // compute error and report
  ierr = TSGetStepNumber(ts,&steps); CHKERRQ(ierr);
  ierr = TSGetTime(ts,&tf); CHKERRQ(ierr);
  ierr = SetFromExact(tf,yexact); CHKERRQ(ierr);
  ierr = VecAXPY(y,-1.0,yexact); CHKERRQ(ierr);    // y <- y - yexact
  ierr = VecNorm(y,NORM_INFINITY,&err); CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
              "error at tf = %.3f with %d steps:  |y-y_exact|_inf = %g\n",
              tf,steps,err); CHKERRQ(ierr);

  MatDestroy(&J);
  VecDestroy(&y);  VecDestroy(&yexact);  TSDestroy(&ts);
  return PetscFinalize();
}

PetscErrorCode SetFromExact(double t, Vec y) {
    double *ay;
    VecGetArray(y,&ay);
    ay[0] = t - sin(t);
    ay[1] = 1.0 - cos(t);
    VecRestoreArray(y,&ay);
    return 0;
}

PetscErrorCode FormRHSFunction(TS ts, double t, Vec y, Vec g, void *ptr) {
    const double *ay;
    double       *ag;
    VecGetArrayRead(y,&ay);
    VecGetArray(g,&ag);
    ag[0] = ay[1];            // = G_1(t,y)
    ag[1] = - ay[0] + t;      // = G_2(t,y)
    VecRestoreArrayRead(y,&ay);
    VecRestoreArray(g,&ag);
    return 0;
}

//STARTJACOBIAN
PetscErrorCode FormRHSJacobian(TS ts, double t, Vec y, Mat J, Mat P,
                               void *ptr) {
    PetscErrorCode ierr;
    int    row[2] = {0, 1},  col[2] = {0, 1};
    double v[4] = { 0.0, 1.0,
                   -1.0, 0.0};
    ierr = MatSetValues(P,2,row,2,col,v,INSERT_VALUES); CHKERRQ(ierr);
    ierr = MatAssemblyBegin(P,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    ierr = MatAssemblyEnd(P,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    if (J != P) {
        ierr = MatAssemblyBegin(J,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
        ierr = MatAssemblyEnd(J,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    }
    return 0;
}
//ENDJACOBIAN

