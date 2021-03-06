#include "StructureFormationProbe.h"

#include "ExtentUtilities.h"
#include "TetrahedronUtilities.h"
#include "VirtualGrid.h"

// C++ includes
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sstream>
#include <string>

namespace cosmologytools {

//------------------------------------------------------------------------------
StructureFormationProbe::StructureFormationProbe()
{
  this->Particles       = NULL;
  this->GlobalIds       = NULL;
  this->Lagrange       = NULL;
  this->VGrid           = NULL;
  this->NumParticles    = 0;
  this->Fringe          = 1;

  this->NumPointsProbed = 0;
  this->NumTetsChecked  = 0;
  this->TimeStepCounter = 0;
}

//------------------------------------------------------------------------------
StructureFormationProbe::~StructureFormationProbe()
{
  if( this->Lagrange != NULL )
    {
    delete this->Lagrange;
    }

  if( this->VGrid != NULL )
    {
    delete this->VGrid;
    }

  this->EulerMesh.Clear();
  this->Volumes.clear();
}

//------------------------------------------------------------------------------
void StructureFormationProbe::SetParticles(
    REAL *particles, INTEGER *globalIds, INTEGER N)
{
  assert("pre: particles array is NULL" && (particles != NULL) );
  assert("pre: global Ids array is NULL" && (globalIds != NULL) );
  assert("pre: Number of particles must be greater than 1" && (N >= 1) );

  this->EulerMesh.Clear();

  this->NumParticles = N;
  this->Particles = particles;
  this->GlobalIds = globalIds;

  for(INTEGER pos=0; pos < N; ++pos )
    {
    this->Global2PositionMap[ globalIds[pos] ] = pos;
    } // END for all particle positions
}


//------------------------------------------------------------------------------
void StructureFormationProbe::BuildLangrangianMesh(
        REAL origin[3], REAL spacing[3], INTEGER extent[6])
{
  if( this->Lagrange != NULL )
    {
    delete this->Lagrange;
    }

  this->Lagrange = new LagrangianTesselator();
  this->Lagrange->SetOrigin(origin);
  this->Lagrange->SetSpacing(spacing);
  this->Lagrange->SetGridExtent(extent);
  this->Lagrange->Tesselate();
}

//------------------------------------------------------------------------------
LagrangianTesselator* StructureFormationProbe::GetLagrangeTesselator()
{
  return this->Lagrange;
}

//------------------------------------------------------------------------------
bool StructureFormationProbe::IsNodeWithinFringeBounds(INTEGER nodeIdx)
{
  assert("pre: Langrange tesselator object is NULL" &&
         (this->Lagrange != NULL) );

  INTEGER idx = this->Global2PositionMap[ nodeIdx ];

  REAL pnt[3];
  pnt[0] = this->Particles[idx*3];
  pnt[1] = this->Particles[idx*3+1];
  pnt[2] = this->Particles[idx*3+2];

  REAL iBounds[6];
  this->Lagrange->GetBounds(iBounds,this->Fringe);

  return(
      (pnt[0] >= iBounds[0]) && (pnt[0] <= iBounds[1]) &&
      (pnt[1] >= iBounds[2]) && (pnt[1] <= iBounds[3]) &&
      (pnt[2] >= iBounds[4]) && (pnt[2] <= iBounds[5])
      );
}

//------------------------------------------------------------------------------
bool StructureFormationProbe::IsNodeWithinPeriodicBoundaryFringe(
          INTEGER nodeIdx )
{
  assert("pre: Langrange tesselator object is NULL" &&
          (this->Lagrange != NULL) );

  INTEGER idx = this->Global2PositionMap[ nodeIdx ];

  REAL pnt[3];
  pnt[0] = this->Particles[idx*3];
  pnt[1] = this->Particles[idx*3+1];
  pnt[2] = this->Particles[idx*3+2];

  REAL bounds[6];
  this->Lagrange->GetBounds(bounds);
  for( int i=0; i < 3; ++i )
    {
    REAL L     = bounds[i*2+1]-bounds[i*2];
    REAL delta = static_cast<REAL>( (L*this->Fringe) )/100.0;
    if( pnt[i] < bounds[i*2]+delta ||
        pnt[i] > bounds[i*2+1]-delta)
      {
      return true;
      } // END if
    } // END for
  return false;
}

//------------------------------------------------------------------------------
bool StructureFormationProbe::MapTetToEulerSpace(
        INTEGER tet[4], REAL nodes[12])
{
  for(int node=0; node < 4; ++node )
    {
    if( this->Global2PositionMap.find(tet[node])==
            this->Global2PositionMap.end() )
      {
      return false;
      }

    if( !this->IsNodeWithinFringeBounds(tet[node]))
      {
      return false;
      }
//    if( this->IsNodeWithinPeriodicBoundaryFringe(tet[node]) )
//      {
//      return false;
//      }

    INTEGER ppos = this->Global2PositionMap[ tet[node] ];
    for( int dim=0; dim < 3; ++dim )
      {
      nodes[ node*3+dim ] = this->Particles[ ppos*3+dim ];
      }

    } // END for all nodes
  return true;
}

//------------------------------------------------------------------------------
void StructureFormationProbe::BuildEulerMesh()
{
  // Sanity check
  assert(
    "pre: Must construct a langrangian mesh first before an euler mesh!" &&
    (this->Lagrange != NULL) );

  this->LagrangeNode2EulerNode.clear();
  this->LagrangeTet2EulerTet.clear();
  this->EulerMesh.Clear();
  this->Volumes.clear();

  // Reserve space to store euler mesh
  this->EulerMesh.Stride = 4;

  // Reserve space for Euler mesh
  this->EulerMesh.Nodes.reserve(
      ExtentUtilities::ComputeNumberOfNodes(
            const_cast<INTEGER*>(this->Lagrange->GetExtent())));
  this->EulerMesh.Connectivity.reserve(
      this->EulerMesh.Stride*this->Lagrange->GetNumTets());
  this->Volumes.reserve(this->Lagrange->GetNumTets());

  for(INTEGER idx=0; idx < this->Lagrange->GetNumTets(); ++idx)
    {
    INTEGER tet[4];
    this->Lagrange->GetTetConnectivity(idx,tet);

    REAL nodes[12];
    if( this->MapTetToEulerSpace(tet,nodes) )
      {
      // Insert vertices & tetrahedra in the mesh
      for( int node=0; node < 4; ++node )
        {
        // Langrange index
        INTEGER lidx = tet[ node ];
        if(this->LagrangeNode2EulerNode.find(lidx)==
           this->LagrangeNode2EulerNode.end())
          {
          this->EulerMesh.Nodes.push_back(nodes[node*3]);
          this->EulerMesh.Nodes.push_back(nodes[node*3+1]);
          this->EulerMesh.Nodes.push_back(nodes[node*3+2]);

          // Euler index
          INTEGER eidx = this->EulerMesh.GetNumberOfNodes()-1;
          this->LagrangeNode2EulerNode[ lidx ] = eidx;
          tet[ node ] = eidx;
          } // END if this node has not been mapped
        else
          {
          tet[ node ] = this->LagrangeNode2EulerNode[ lidx ];
          } // END else if node has already been mapped

        this->EulerMesh.Connectivity.push_back( tet[ node ] );
        } // END for all tet nodes

      this->LagrangeTet2EulerTet[idx]=this->EulerMesh.GetNumberOfCells()-1;

      // Compute the volume of the added tetrahedron
      this->Volumes.push_back(
          TetrahedronUtilities::ComputeVolume(
              &nodes[0],&nodes[3],&nodes[6],&nodes[9]) );

      } // END if the tet is mapped succesfully to euler space.

    // The following loop-invariant must hold for correctness
    assert("post: volumes array must be equal to the number of tets!" &&
            this->Volumes.size()==this->EulerMesh.GetNumberOfCells());

    } // END for all langrangian tets

  // Build virtual-grid that covers for this euler mesh instance
  this->BuildVirtualGrid();
  ++this->TimeStepCounter;
}

//------------------------------------------------------------------------------
void StructureFormationProbe::BuildVirtualGrid()
{
  assert( "pre: Langrangian mesh must be constructed!" &&
          (this->Lagrange != NULL) );

  if( this->VGrid != NULL )
    {
    delete this->VGrid;
    }

  INTEGER dims[3];
  ExtentUtilities::GetExtentDimensions(
      const_cast<INTEGER*>(this->Lagrange->GetExtent()),dims);

  this->VGrid = new VirtualGrid();
  this->VGrid->SetDimensions(dims);
  this->VGrid->RegisterMesh(this->EulerMesh);

//  std::ostringstream oss;
//  oss << "vgrid_" << this->TimeStepCounter << ".vtk";
//  std::ofstream ofs;
//  ofs.open(oss.str().c_str());
//  ofs << this->VGrid->ToLegacyVtkString() << std::endl;
//  ofs.close();
}

//------------------------------------------------------------------------------
void StructureFormationProbe::ProbePoint(
    REAL pnt[3], INTEGER &nStreams, REAL &rho)
{
  // STEP 0: If a virtual grid is not built, build it
  if( this->VGrid == NULL )
    {
    this->BuildVirtualGrid();
    }

  ++this->NumPointsProbed;

  // STEP 1: Initialize output variables
  nStreams = 0;
  rho      = 0.0;

  REAL volumeSum = 0.0; // sum of all tet volumes containing the point, needed
                        // to compute rho

  // STEP 2: Get the candidate cells for the given point
  std::vector<INTEGER> tets;
  this->VGrid->GetCandidateCellsForPoint(pnt,tets);

  // STEP 3: Loop through all candidate cells and:
  // (1) Compute the number of streams
  // (2) Compute the local density, rho
  for(unsigned int t=0; t < tets.size(); ++t)
    {
    ++this->NumTetsChecked;

    INTEGER tetIdx = tets[ t ];
    if( this->PointInTet(pnt,tetIdx) )
      {
      ++nStreams;
      volumeSum += std::fabs(this->Volumes[tetIdx]);
      }
    } // END for all candidate cells

  // STEP 4: If the number of streams is greater than 1, compute rho
  if( nStreams > 0 )
    {
    rho = ( volumeSum/static_cast<REAL>(nStreams) );
    }

}

//------------------------------------------------------------------------------
bool StructureFormationProbe::PointInTet(REAL pnt[3], INTEGER tetIdx)
{
  // STEP 0: Get the coordinates of the tetrahderon nodes
  REAL V0[3]; REAL V1[3]; REAL V2[3]; REAL V3[3];
  this->EulerMesh.GetTetNodes(tetIdx,V0,V1,V2,V3);

  // STEP 1: Check if the point is inside the tet. First do a fast approximate
  // bounding-box test and if that passes, do the more expensive HasPoint test
  if( TetrahedronUtilities::PointInTetBoundingBox(pnt,V0,V1,V2,V3) &&
      TetrahedronUtilities::HasPoint(pnt,V0,V1,V2,V3) )
    {
    return true;
    }
  return false;
}

//------------------------------------------------------------------------------
void StructureFormationProbe::GetEulerMesh(
        std::vector<REAL> &nodes, std::vector<INTEGER> &tets,
        std::vector<REAL> &volumes)
{
  // Copy internal data-structures to user-supplied vectors
  nodes   = this->EulerMesh.Nodes;
  tets    = this->EulerMesh.Connectivity;
  volumes = this->Volumes;
}

//------------------------------------------------------------------------------
void StructureFormationProbe::ExtractCausticSurfaces(
        std::vector<REAL> &nodes, std::vector<INTEGER> &triangles)
{
  // Sanity check
  assert("pre: Must construct a langrangian mesh first before an euler mesh!"
         && (this->Lagrange != NULL) );

  // STEP 0: Initialize supplied vectors
  nodes.clear();
  triangles.clear();

  // STEP 1: Get Langrange Mesh faces
  std::vector< INTEGER > faces;
  this->Lagrange->GetFaces( faces );

  // STEP 2: Loop through Langrange mesh faces and determine if they correspond
  // to a face on the caustics surface mesh.
  std::vector<INTEGER> tets;
  INTEGER face[3];
  for(INTEGER fidx=0; fidx < faces.size()/3; ++fidx)
    {
    for( int node=0; node < 3; ++node )
      {
      face[node] = faces[fidx*3+node];
      } // END for all face nodes

    this->Lagrange->GetAdjacentTets(face,tets);
    assert("pre: face has more than 2 adjacent tets!" &&
            (tets.size() >= 1) && (tets.size() <= 2) );

    if( tets.size() != 2 )
      {
      // This is a boundary face, skip!
      continue;
      }

    if( (this->LagrangeTet2EulerTet.find(tets[0])==
         this->LagrangeTet2EulerTet.end()) ||
        (this->LagrangeTet2EulerTet.find(tets[1]) ==
         this->LagrangeTet2EulerTet.end()) )
      {
      // This face abutts a tet that wasn't mapped to the euler mesh, skip!
      continue;
      }

    // Get the tet indices in the euler mesh
    INTEGER tetIdx1 = this->LagrangeTet2EulerTet[tets[0]];
    INTEGER tetIdx2 = this->LagrangeTet2EulerTet[tets[1]];

    std::map<INTEGER,INTEGER> eulerMesh2CausticSurface;

    if( this->VolumesHaveOppositeSigns(tetIdx1,tetIdx2) )
      {
      INTEGER t[3];
      for( int tnode=0; tnode < 3; ++tnode)
        {
        assert("caustics surface mesh node cannot be found in Euler mesh!" &&
                this->LagrangeNode2EulerNode.find(face[tnode]) !=
                this->LagrangeNode2EulerNode.end());

        // Get the node index w.r.t. the euler mesh
       INTEGER nodeIdx = this->LagrangeNode2EulerNode[face[tnode]];

        if( eulerMesh2CausticSurface.find(nodeIdx) ==
             eulerMesh2CausticSurface.end() )
          {
          nodes.push_back(this->EulerMesh.Nodes[nodeIdx*3 ]);
          nodes.push_back(this->EulerMesh.Nodes[nodeIdx*3+1]);
          nodes.push_back(this->EulerMesh.Nodes[nodeIdx*3+2]);
          eulerMesh2CausticSurface[nodeIdx] = (nodes.size()/3)-1;
          } // END if
        t[tnode] = eulerMesh2CausticSurface[nodeIdx];
        triangles.push_back( t[tnode] );
        } // END for all tnodes

      } // END if the volumes of the adjacent tets is opposite

    } // END for all mesh faces
}

//------------------------------------------------------------------------------
bool StructureFormationProbe::VolumesHaveOppositeSigns(
        INTEGER tetIdx1, INTEGER tetIdx2)
{
  assert("pre: tet index 1 is out-of-bounds!" &&
     (tetIdx1 >= 0) && (tetIdx1 < this->Volumes.size() ) );
  assert("pre: tet index 2 is out-of-bounds!" &&
     (tetIdx2 >= 0) && (tetIdx2 < this->Volumes.size() ) );

  REAL v1 = this->Volumes[tetIdx1];
  REAL v2 = this->Volumes[tetIdx2];
  return( ((v1*v2) < 0.0f) );
}

} /* namespace cosmologytools */
