#include <vtkPVoronoiReader.h>
 
#include <vtkObjectFactory.h>
#include <vtkStreamingDemandDrivenPipeline.h>
#include <vtkInformationVector.h>
#include <vtkInformation.h>
#include <vtkDataObject.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkMultiProcessController.h>
#include <vtkUnstructuredGrid.h>
#include <vtkCellArray.h>
#include <vtkFloatArray.h>
#include <vtkPoints.h>
#include <vtkFieldData.h>

#include <iostream>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <assert.h>
using namespace std;

//#include <vtkSmartPointer.h>
#define VTK_CREATE(type, name) \
    vtkSmartPointer<type> name = vtkSmartPointer<type>::New()
#define VTK_NEW(type, name) \
    name = vtkSmartPointer<type>::New()
 
vtkStandardNewMacro(vtkPVoronoiReader);
 
vtkPVoronoiReader::vtkPVoronoiReader()
{
  this->swap_bytes = 0;
  this->FileName = NULL;
  this->SetNumberOfInputPorts(0);
  this->SetNumberOfOutputPorts(1);
  this->Controller = NULL;
  this->SetController(vtkMultiProcessController::GetGlobalController());
}

void vtkPVoronoiReader::SetController(vtkMultiProcessController *c)
{
  if ((c == NULL) || (c->GetNumberOfProcesses() == 0))
  {
    this->NumProcesses = 1;
    this->MyId = 0;
  }

  if (this->Controller == c)
  {
    return;
  }

  this->Modified();

  if (this->Controller != NULL)
  {
    this->Controller->UnRegister(this);
    this->Controller = NULL;
  }

  if (c == NULL)
  {
    return;
  }

  this->Controller = c;

  c->Register(this);
  this->NumProcesses = c->GetNumberOfProcesses();
  this->MyId = c->GetLocalProcessId();
}
 
int vtkPVoronoiReader::RequestData(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **vtkNotUsed(inputVector),
  vtkInformationVector *outputVector)
{
  int i, j;
  // get the info object
  vtkInformation *outInfo = outputVector->GetInformationObject(0);
  int piece, numPieces;
 
  piece = outInfo->Get(
      vtkStreamingDemandDrivenPipeline::UPDATE_PIECE_NUMBER());
  numPieces = outInfo->Get(
      vtkStreamingDemandDrivenPipeline::UPDATE_NUMBER_OF_PIECES());
 
  vtkMultiBlockDataSet *output = vtkMultiBlockDataSet::SafeDownCast(
      outInfo->Get(vtkDataObject::DATA_OBJECT()));

  //read file
  vtkMultiProcessController *contr = this->Controller;

  int sum = 0;
  int oops = ((piece != this->MyId) || (numPieces != this->NumProcesses));

  contr->Reduce(&oops, &sum, 1, vtkCommunicator::SUM_OP, 0);
  contr->Broadcast(&sum, 1, 0);

  if (sum > 0) //from example, not sure whether to happen
  {
    cout << "sum > 0" << endl;
  }

  if (!contr) //from example, not sure whether to happen
  {
    //this->SetUpEmptyGrid(output);
    return 1;
  }

  FILE *fd = fopen(FileName, "r");
  assert(fd != NULL);
  int64_t *ftr;   // footer
  int tb;         // total number of blocks
  int root = 0;   // root rank
  if (piece == root)
    ReadFooter(fd, ftr, tb);
  contr->Barrier();

  contr->Broadcast(&tb, 1, root);
  if (piece != root)
    ftr = new int64_t[tb];
  contr->Broadcast((unsigned char *)ftr, sizeof(int64_t) / sizeof(unsigned char) * tb, root);

  vblock_t *block;
  char msg[100];
  output->SetNumberOfBlocks(tb);
  for (i=piece; i<tb; i+=numPieces)
  {
    sprintf(msg, "rank %d, block %d, offset %ld\n", piece, i, ftr[i]);
    printf("%s", msg);
    block = (vblock_t *)malloc(sizeof(vblock_t));
    ReadBlock(fd, block, ftr[i]);
    VTK_CREATE(vtkUnstructuredGrid, ugrid);
    vor2ugrid(block, ugrid);
    output->SetBlock(i, ugrid);
  }
  
  fclose(fd);

  if (contr != this->Controller)
  {
    contr->Delete();
  }

  delete ftr;

  return 1;
}
 
void vtkPVoronoiReader::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
 
  os << indent << "File Name: "
      << (this->FileName ? this->FileName : "(none)") << "\n";
}

//SER_IO related

//----------------------------------------------------------------------------
//
// reads the file footer
// footer in file is always ordered by global block id
// output footer is in the same order
//
// fd: open file
// ftr: footer data (output)
// tb: total number of blocks in the file (output)
//
// side effects: allocates ftr
//
void vtkPVoronoiReader::ReadFooter(FILE*& fd, int64_t*& ftr, int& tb)
{
  int ofst;
  int count;
  int64_t temp;

  ofst = sizeof(int64_t);
  fseek(fd, -ofst, SEEK_END);
  count = fread(&temp, sizeof(int64_t), 1, fd); // total number of blocks
  assert(count == 1); // total number of blocks

  if (swap_bytes)
    Swap((char *)&temp, 1, sizeof(int64_t));
  tb = temp;

  if (tb > 0) {

    ftr = new int64_t[tb];
    ofst = (tb + 1) * sizeof(int64_t);
    fseek(fd, -ofst, SEEK_END);
    count = fread(ftr, sizeof(int64_t), tb, fd);
    assert(count == tb);

    if (swap_bytes)
      Swap((char *)ftr, tb, sizeof(int64_t));
  }
}
//----------------------------------------------------------------------------
//
// reads the header for one block from a file
//
// fd: open file
// hdr: allocated header data
// ofst: location in file of the header (bytes)
//
void vtkPVoronoiReader::ReadHeader(FILE *fd, int *hdr, int64_t ofst)
{
  int count;

  fseek(fd, ofst, SEEK_SET);
  count = fread(hdr, sizeof(int), HDR_ELEMS, fd);
  assert(count == HDR_ELEMS);

  if (swap_bytes)
    Swap((char *)hdr, HDR_ELEMS, sizeof(int));
}
//----------------------------------------------------------------------------
//
// Copies the header for one block from a buffer in memory
//
// in_buf: input buffer location
// hdr: allocated header data
//
// returns: number of bytes copies
//
int vtkPVoronoiReader::CopyHeader(unsigned char *in_buf, int *hdr)
{
  memcpy(hdr, in_buf, HDR_ELEMS * sizeof(int));

  if (swap_bytes)
    Swap((char *)hdr, HDR_ELEMS, sizeof(int));

  return(HDR_ELEMS * sizeof(int));
}
//----------------------------------------------------------------------------
//
// reads one block from a file
//
// fd: open file
// v: pointer to output block
// ofst: file file pointer to start of header for this block
//
// side-effects: allocates block
//
void vtkPVoronoiReader::ReadBlock(FILE *fd, vblock_t* &v, int64_t ofst)
{
  // get header info
  int hdr[HDR_ELEMS];
  ReadHeader(fd, hdr, ofst);

  // create block
  v = new vblock_t;
  v->num_verts = hdr[NUM_VERTS];
  v->num_cells = hdr[NUM_CELLS];
  v->tot_num_cell_verts = hdr[TOT_NUM_CELL_VERTS];
  v->num_complete_cells = hdr[NUM_COMPLETE_CELLS];
  v->tot_num_cell_faces = hdr[TOT_NUM_CELL_FACES];
  v->tot_num_face_verts = hdr[TOT_NUM_FACE_VERTS];
  v->num_orig_particles = hdr[NUM_ORIG_PARTICLES];

  if (v->num_verts > 0)
    v->save_verts = new float[3 * v->num_verts];
  if (v->num_cells > 0) {
    v->num_cell_verts = new int[v->num_cells];
    v->sites = new float[3 * v->num_orig_particles];
  }
  if (v->tot_num_cell_verts > 0)
    v->cells = new int[v->tot_num_cell_verts];
  if (v->num_complete_cells > 0) {
    v->complete_cells = new int[v->num_complete_cells];
    v->areas = new float[v->num_complete_cells];
    v->vols = new float[v->num_complete_cells];
    v->num_cell_faces = new int[v->num_complete_cells];
  }
  if (v->tot_num_cell_faces > 0)
    v->num_face_verts = new int[v->tot_num_cell_faces];
  if (v->tot_num_face_verts > 0)
    v->face_verts = new int[v->tot_num_face_verts];

  fread(v->mins, sizeof(float), 3, fd);
  fread(v->save_verts, sizeof(float), 3 * v->num_verts, fd);
  fread(v->sites, sizeof(float), 3 * v->num_orig_particles, fd);
  fread(v->complete_cells, sizeof(int), v->num_complete_cells, fd);
  fread(v->areas, sizeof(float), v->num_complete_cells, fd);
  fread(v->vols, sizeof(float), v->num_complete_cells, fd);
  fread(v->num_cell_faces, sizeof(int), v->num_complete_cells, fd);
  fread(v->num_face_verts, sizeof(int), v->tot_num_cell_faces, fd);
  fread(v->face_verts, sizeof(int), v->tot_num_face_verts, fd);
  fread(v->maxs, sizeof(float), 3, fd);

  if (swap_bytes) {
    Swap((char *)v->mins, 3, sizeof(float));
    Swap((char *)v->save_verts, 3 * v->num_verts, sizeof(float));
    Swap((char *)v->sites, 3 * v->num_orig_particles, sizeof(float));
    Swap((char *)v->complete_cells, v->num_complete_cells, sizeof(int));
    Swap((char *)v->areas, v->num_complete_cells, sizeof(float));
    Swap((char *)v->vols, v->num_complete_cells, sizeof(float));
    Swap((char *)v->num_cell_faces, v->num_complete_cells, sizeof(int));
    Swap((char *)v->num_face_verts, v->tot_num_cell_faces, sizeof(int));
    Swap((char *)v->face_verts, v->tot_num_face_verts, sizeof(int));
    Swap((char *)v->maxs, 3, sizeof(float));
  }
}
//----------------------------------------------------------------------------
//
// copies one block from a buffer in memory
//
// in_buf: input buffer location
// m: pointer to output block
// ofst: file file pointer to start of header for this block
//
// side-effects: allocates block
//
void vtkPVoronoiReader::CopyBlock(unsigned char *in_buf, vblock_t* &v)
{
  int ofst = 0; // offset in buffer for next section of data to read

  // get header info
  int hdr[HDR_ELEMS];
  ofst += CopyHeader(in_buf, hdr);

  // create block
  v = new vblock_t;
  v->num_verts = hdr[NUM_VERTS];
  v->num_cells = hdr[NUM_CELLS];
  v->tot_num_cell_verts = hdr[TOT_NUM_CELL_VERTS];
  v->num_complete_cells = hdr[NUM_COMPLETE_CELLS];
  v->tot_num_cell_faces = hdr[TOT_NUM_CELL_FACES];
  v->tot_num_face_verts = hdr[TOT_NUM_FACE_VERTS];
  v->num_orig_particles = hdr[NUM_ORIG_PARTICLES];

  if (v->num_verts > 0)
    v->save_verts = new float[3 * v->num_verts];
  if (v->num_cells > 0) {
    v->num_cell_verts = new int[v->num_cells];
    v->sites = new float[3 * v->num_orig_particles];
  }
  if (v->tot_num_cell_verts > 0)
    v->cells = new int[v->tot_num_cell_verts];
  if (v->num_complete_cells > 0) {
    v->complete_cells = new int[v->num_complete_cells];
    v->areas = new float[v->num_complete_cells];
    v->vols = new float[v->num_complete_cells];
    v->num_cell_faces = new int[v->num_complete_cells];
  }
  if (v->tot_num_cell_faces > 0)
    v->num_face_verts = new int[v->tot_num_cell_faces];
  if (v->tot_num_face_verts > 0)
    v->face_verts = new int[v->tot_num_face_verts];

    memcpy(&v->mins, in_buf + ofst, 3 * sizeof(float));
    ofst += (3 * sizeof(float));

    memcpy(v->save_verts, in_buf + ofst, 3 * v->num_verts * sizeof(float));
    ofst += (3 * v->num_verts * sizeof(float));

    memcpy(v->sites, in_buf + ofst, 3 * v->num_orig_particles * sizeof(float));
    ofst += (3 * v->num_orig_particles * sizeof(float));

    memcpy(v->complete_cells, in_buf + ofst, 
	   v->num_complete_cells * sizeof(int));
    ofst += (v->num_complete_cells * sizeof(int));

    memcpy(v->areas, in_buf + ofst, v->num_complete_cells * sizeof(float));
    ofst += (v->num_complete_cells * sizeof(float));

    memcpy(v->vols, in_buf + ofst, v->num_complete_cells * sizeof(float));
    ofst += (v->num_complete_cells * sizeof(float));

    memcpy(v->num_cell_faces, in_buf + ofst, 
	   v->num_complete_cells * sizeof(int));
    ofst += (v->num_complete_cells * sizeof(int));

    memcpy(v->num_face_verts, in_buf + ofst, 
	   v->tot_num_cell_faces * sizeof(int));
    ofst += (v->tot_num_cell_faces * sizeof(int));

    memcpy(v->face_verts, in_buf + ofst, v->tot_num_face_verts * sizeof(int));
    ofst += (v->tot_num_face_verts * sizeof(int));

    memcpy(&v->maxs, in_buf + ofst, 3 * sizeof(float));
    ofst += (3 * sizeof(float));

  if (swap_bytes) {
    Swap((char *)&v->mins, 3, sizeof(float));
    Swap((char *)v->save_verts, 3 * v->num_verts, sizeof(float));
    Swap((char *)v->sites, 3 * v->num_orig_particles, sizeof(float));
    Swap((char *)v->complete_cells, v->num_complete_cells, sizeof(int));
    Swap((char *)v->areas, v->num_complete_cells, sizeof(float));
    Swap((char *)v->vols, v->num_complete_cells, sizeof(float));
    Swap((char *)v->num_cell_faces, v->num_complete_cells, sizeof(int));
    Swap((char *)v->num_face_verts, v->tot_num_cell_faces, sizeof(int));
    Swap((char *)v->face_verts, v->tot_num_face_verts, sizeof(int));
    Swap((char *)&v->maxs, 3, sizeof(float));
  }
}
//----------------------------------------------------------------------------

#if 0 // not using compression for now

//
// block decompression
//
// in_buf: input block buffer (MPI_BYTE datatype)
// in_size: input size in bytes
// decomp_buf: decompressed buffer
// decomp_size: decompressed size in bytes (output)
//
// side effects: grows decomp_buf, user's responsibility to free it
//
void vtkPVoronoiReader::DecompressBlock(unsigned  char* in_buf, int in_size, 
			    vector<unsigned char> *decomp_buf, 
			    int *decomp_size) {

  unsigned int out_size; // size of output buffer
  z_stream strm; // structure used to pass info t/from zlib
  unsigned char temp_buf[CHUNK]; // temporary
  int num_out; // number of compressed bytes in one deflation
  int ret; // return value

  // allocate inflate state
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;
  ret = inflateInit(&strm);
  assert(ret == Z_OK);

  *decomp_size = 0;
  int num_in_chunks = ceil((float)in_size / CHUNK); // number of input chunks

  // read the character buffer in chunks
  for (int i = 0; i < num_in_chunks; i++) { 
    strm.next_in = in_buf + i * CHUNK;
    if (i < num_in_chunks - 1) 
      strm.avail_in = CHUNK;
    else
      strm.avail_in = (in_size % CHUNK ? in_size - i * CHUNK : CHUNK);
    do { // decompress each chunk
      strm.avail_out = CHUNK;
      strm.next_out = temp_buf;
      ret = inflate(&strm, Z_NO_FLUSH);
      assert(ret != Z_STREAM_ERROR);
      num_out = CHUNK - strm.avail_out;
      *decomp_size += num_out;
      decomp_buf->insert(decomp_buf->end(), temp_buf, temp_buf + num_out);
    } while (strm.avail_out == 0);
    if (ret == Z_STREAM_END) // in case decompression finishes early
      break;
  }
  inflateEnd(&strm);

//   fprintf(stderr, "decompression: from %d bytes to %d bytes\n", 
// 	  in_size, *decomp_size);

}
//-----------------------------------------------------------------------

#endif

// SWAP related

// swaps bytes
//
// n: address of items
// nitems: number of items
// item_size: either 2, 4, or 8 bytes
// returns quietly if item_size is 1
//
void vtkPVoronoiReader::Swap(char *n, int nitems, int item_size)
{
  int i;

  switch(item_size) {
  case 1:
    break;
  case 2:
    for (i = 0; i < nitems; i++) {
      Swap2(n);
      n += 2;
    }
    break;
  case 4:
    for (i = 0; i < nitems; i++) {
      Swap4(n);
      n += 4;
    }
    break;
  case 8:
    for (i = 0; i < nitems; i++) {
      Swap8(n);
      n += 8;
    }
    break;
  default:
    fprintf(stderr, "Error: size of data must be either 1, 2, 4, or 8 bytes per item\n");
    //MPI_Abort(MPI_COMM_WORLD, 0);
  }
}
//-----------------------------------------------------------------------
//
// Swaps 8  bytes from 1-2-3-4-5-6-7-8 to 8-7-6-5-4-3-2-1 order.
// cast the input as a char and use on any 8 byte variable
//
void vtkPVoronoiReader::Swap8(char *n)
{
  char *n1;
  char c;

  n1 = n + 7;
  c = *n;
  *n = *n1;
  *n1 = c;

  n++;
  n1--;
  c = *n;
  *n = *n1;
  *n1 = c;

  n++;
  n1--;
  c = *n;
  *n = *n1;
  *n1 = c;

  n++;
  n1--;
  c = *n;
  *n = *n1;
  *n1 = c;
}
//-----------------------------------------------------------------------------
//
// Swaps 4 bytes from 1-2-3-4 to 4-3-2-1 order.
// cast the input as a char and use on any 4 byte variable
//
void vtkPVoronoiReader::Swap4(char *n)
{
  char *n1;
  char c;

  n1 = n + 3;
  c = *n;
  *n = *n1;
  *n1 = c;

  n++;
  n1--;
  c = *n;
  *n = *n1;
  *n1 = c;
}
//----------------------------------------------------------------------------
//
// Swaps 2 bytes from 1-2 to 2-1 order.
// cast the input as a char and use on any 2 byte variable
//
void vtkPVoronoiReader::Swap2(char *n)
{
  char c;

  c = *n;
  *n = n[1];
  n[1] = c;
}

void vtkPVoronoiReader::vor2ugrid(struct vblock_t *block, vtkSmartPointer<vtkUnstructuredGrid> &ugrid)
{
  int i, j, k;
  int num_cells, num_verts, num_faces;
  char msg[100];

  num_cells = block->num_complete_cells;
  num_verts = block->num_verts;
  num_faces = block->tot_num_cell_faces;

  float *vert_vals = block->save_verts;

  float mins[3], maxs[3];
  mins[0] = 0;  mins[1] = 0;  mins[2] = 0; //hack
  maxs[0] = 32; maxs[1] = 32; maxs[2] = 32;
  for (i=0; i<num_verts; i++)
  {
    if (vert_vals[i * 3    ] < mins[0]) vert_vals[i * 3    ] = mins[0];
    if (vert_vals[i * 3    ] > maxs[0]) vert_vals[i * 3    ] = maxs[0];
    if (vert_vals[i * 3 + 1] < mins[1]) vert_vals[i * 3 + 1] = mins[1];
    if (vert_vals[i * 3 + 1] > maxs[1]) vert_vals[i * 3 + 1] = maxs[1];
    if (vert_vals[i * 3 + 2] < mins[2]) vert_vals[i * 3 + 2] = mins[2];
    if (vert_vals[i * 3 + 2] > maxs[2]) vert_vals[i * 3 + 2] = maxs[2];
  }

  //create points
  VTK_CREATE(vtkFloatArray, points_array);
  points_array->SetNumberOfComponents(3);
  points_array->SetNumberOfTuples(num_verts);
  points_array->SetVoidArray(vert_vals, 3 * num_verts, 0);

  VTK_CREATE(vtkPoints, points);
  points->SetData(points_array);
 
  //create unstructure grid
  //VTK_CREATE(vtkUnstructuredGrid, ugrid);
  ugrid->SetPoints(points);

  //cells
  int vert_id, face_id, num_cell_faces, num_face_verts, num_tot_fverts;
  int vert_id_cell_start, num_cell_fverts;
  vtkIdType *face_verts;

  vert_id = 0;
  face_id = 0;
  num_tot_fverts = block->tot_num_face_verts;

  face_verts = (vtkIdType *)malloc(sizeof(vtkIdType) * num_tot_fverts);
  for (j=0; j<num_tot_fverts; j++)
    face_verts[j] = block->face_verts[j];

  for (j=0; j<num_cells; j++)
  {
    VTK_CREATE(vtkCellArray, cell);
    num_cell_faces = block->num_cell_faces[j];

    vert_id_cell_start = vert_id;
    num_cell_fverts = 0;

    for (k=0; k<num_cell_faces; k++)
    {
      num_face_verts = block->num_face_verts[face_id];
      cell->InsertNextCell(num_face_verts, &face_verts[vert_id]);

      face_id ++;
      vert_id += num_face_verts;
      num_cell_fverts += num_face_verts;
    }

    ugrid->InsertNextCell(VTK_POLYHEDRON, num_cell_fverts, &face_verts[vert_id_cell_start], num_cell_faces, cell->GetPointer());
  }

  //fields, area, vol
  float *field_vals = (float *)malloc(sizeof(float) * 2 * num_cells);
  for (i=0; i<num_cells; i++)
  {
    field_vals[i * 2] = i;
    field_vals[i * 2 + 1] = i + 0.5;
  }

  VTK_CREATE(vtkFloatArray, field_array);
  field_array->SetNumberOfComponents(2);
  field_array->SetNumberOfTuples(num_cells);
  field_array->SetVoidArray(field_vals, 2 * num_cells, 0);

  ugrid->GetFieldData()->AddArray(field_array);

  //if (MyId == 5)
  //  ugrid->Print(cout);
}