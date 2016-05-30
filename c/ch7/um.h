#ifndef UM_H_
#define UM_H_

//STARTSTRUCT
// location of one node
typedef struct {
    double   x,y;
} Node;

// data type for an Unstructured Mesh
typedef struct {
    int      N,     // number of nodes
             K,     // number of elements
             P;     // number of boundary segments
    Vec      loc;   // nodal locations; length N, dof=2 Vec, with Node entries
    IS       e,     // element triples; length 3K
                    //     values e[3*k+0],e[3*k+1],e[3*k+2]
                    //     are indices into node-based Vecs
             bfn,   // flag for boundary node; length N
                    //     if abfn[i] > 0 then node i is on boundary
                    //     if abfn[i] == 2 then node i is on Dirichlet bdry
             s,     // boundary segment pairs; length 2P
                    //     values s[2*p+0],s[2*p+1]
                    //     are indices into node-based Vecs
             bfs;   // flag for boundary segment; length P
                    //     if abfs[p] == 1 then segment p is on Neumann bdry
                    //     if abfs[p] == 2 then segment p is on Dirichlet bdry
} UM;
//ENDSTRUCT

//"methods" below are listed in typical call order

//STARTDECLARE
PetscErrorCode UMInitialize(UM *mesh);  // call first

// create Vec and then read node coordinates from file into it
PetscErrorCode UMReadNodes(UM *mesh, char *filename);

// create ISs and then read element triples, boundary segments, and boundary
// flags into them; call UMReadNodes() first
PetscErrorCode UMReadISs(UM *mesh, char *filename);

// view all fields in UM to the viewer
PetscErrorCode UMView(UM *mesh, PetscViewer viewer);

// give access to a length-N array of Node structs
PetscErrorCode UMGetNodeCoordArrayRead(UM *mesh, const Node **xy);
PetscErrorCode UMRestoreNodeCoordArrayRead(UM *mesh, const Node **xy);

PetscErrorCode UMDestroy(UM *mesh);  // call last
//ENDDECLARE
#endif
