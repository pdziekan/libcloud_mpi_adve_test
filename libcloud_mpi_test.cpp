#include <libcloudph++/lgrngn/factory.hpp>
#include <boost/assign/ptr_map_inserter.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <libcloudph++/common/hydrostatic.hpp>
#include <libcloudph++/common/theta_std.hpp>
#include <libcloudph++/common/theta_dry.hpp>
#include <libcloudph++/common/lognormal.hpp>
#include <libcloudph++/common/unary_function.hpp>
#include <iostream>
#include "mpi.h"


using namespace std;
using namespace libcloudphxx::lgrngn;
  namespace hydrostatic = libcloudphxx::common::hydrostatic;
  namespace theta_std = libcloudphxx::common::theta_std;
  namespace theta_dry = libcloudphxx::common::theta_dry;
  namespace lognormal = libcloudphxx::common::lognormal;

  //aerosol bimodal lognormal dist. 
  const quantity<si::length, double>
    mean_rd1 = double(30e-6) * si::metres,
    mean_rd2 = double(40e-6) * si::metres;
  const quantity<si::dimensionless, double>
    sdev_rd1 = double(1.4),
    sdev_rd2 = double(1.6);
  const quantity<power_typeof_helper<si::length, static_rational<-3>>::type, double>
    n1_stp = double(60e6) / si::cubic_metres,
    n2_stp = double(40e6) / si::cubic_metres;


/*
  // lognormal aerosol distribution
  template <typename T>
  struct log_dry_radii : public libcloudphxx::common::unary_function<T>
  {
    const quantity<si::length, real_t> mean_rd1, mean_rd2;
    const quantity<si::dimensionless, real_t> sdev_rd1, sdev_rd2;
    const quantity<power_typeof_helper<si::length, static_rational<-3>>::type, real_t> n1_stp, n2_stp;

    log_dry_radii(
      quantity<si::length, real_t> mean_rd1,
      quantity<si::length, real_t> mean_rd2,
      quantity<si::dimensionless, real_t> sdev_rd1,
      quantity<si::dimensionless, real_t> sdev_rd2,
      quantity<power_typeof_helper<si::length, static_rational<-3>>::type, real_t> n1_stp,
      quantity<power_typeof_helper<si::length, static_rational<-3>>::type, real_t> n2_stp
    ):
    mean_rd1(mean_rd1),
    mean_rd2(mean_rd2),
    sdev_rd1(sdev_rd1),
    sdev_rd2(sdev_rd2),
    n1_stp(n1_stp),
    n2_stp(n2_stp) {}


    T funval(const T lnrd) const
    {
      return T((
          lognormal::n_e(mean_rd1, sdev_rd1, n1_stp, quantity<si::dimensionless, real_t>(lnrd)) +
          lognormal::n_e(mean_rd2, sdev_rd2, n2_stp, quantity<si::dimensionless, real_t>(lnrd)) 
        ) * si::cubic_metres
      );
    }
  };
*/

// lognormal aerosol distribution
template <typename T>
struct log_dry_radii : public libcloudphxx::common::unary_function<T>
{
  T funval(const T lnrd) const
  {   
    return T(( 
        lognormal::n_e(mean_rd1, sdev_rd1, n1_stp, quantity<si::dimensionless, double>(lnrd)) +
        lognormal::n_e(mean_rd2, sdev_rd2, n2_stp, quantity<si::dimensionless, double>(lnrd)) 
      ) * si::cubic_metres
    );  
  }   
};  


void two_step(particles_proto_t<double> *prtcls, 
             arrinfo_t<double> th,
             arrinfo_t<double> rhod,
             arrinfo_t<double> rv,
             arrinfo_t<double> Cx,
             arrinfo_t<double> Cy,
             arrinfo_t<double> Cz  ,
             opts_t<double> opts)
{
    prtcls->step_sync(opts,th,rv,rhod,Cx, Cy, Cz);
    prtcls->step_async(opts);
}

int m1(int a)
{
  return a > 1? a : 1;
}


//const int nx_factor = 6;
const int nx_min = 3;

void test(backend_t backend, int ndims, bool dir, int n_devices) // n_devices - number of GPUs used per node, each has to be controlled by a single process
{
  int rank = -1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int size = -1;
  MPI_Comm_size( MPI_Comm comm, &size ) 
/*
  if(rank>1)
  {
    throw std::runtime_error("This test doesn't work for more than 2 mpi processes\n");
  }
*/
  if(rank==0)
  {
//    std::cout << std::endl << " ------------------------------------ " << std::endl;
    std::cout << "ndims: " << ndims <<  " direction: " << dir << " backend: " << backend << std::endl;
  } 
  MPI_Barrier(MPI_COMM_WORLD);

  opts_init_t<double> opts_init;
  opts_init.dt=3.;
  opts_init.sstp_coal = 1; 
  opts_init.kernel = kernel_t::geometric;
  opts_init.terminal_velocity = vt_t::beard76;
  opts_init.dx = 1;
  opts_init.nx = rank+nx_min;// nx_factor/2*(rank/2+1); // previously, two GPUs on the same node had the same nx - why? 
  int nx_total = (nx_min. + (nx_min. + size - 1)) / 2. * size;
  opts_init.x1 = opts_init.nx * opts_init.dx;// nx_factor/2*(rank/2+1);
  opts_init.sd_conc = 64;
  opts_init.n_sd_max = 1000*opts_init.sd_conc;
  opts_init.rng_seed = 4444;// + rank;
  if(ndims>1)
  {
    opts_init.dz = 1; 
    opts_init.nz = 4; 
    opts_init.z1 = 4; 
  }
  if(ndims==3)
  {
    opts_init.dy = 1; 
    opts_init.ny = 5; 
    opts_init.y1 = 5; 
  }
  opts_init.dev_id = rank%n_devices; 
  //opts_init.dev_id = rank; 
  std::cout << "device id: " << opts_init.dev_id << std::endl;
//  opts_init.sd_const_multi = 1;

/*
  boost::assign::ptr_map_insert<
    log_dry_radii<double> // value type
  >(  
    opts_init.dry_distros // map
  )(  
    0.001 // key
  ); 
*/

  opts_init.dry_distros.emplace( 
    0.001, // kappa
    std::make_shared<log_dry_radii<double>>() // distribution
  );

  particles_proto_t<double> *prtcls;

/*
  printf("nx = %d\n", opts_init.nx);
  printf("ny = %d\n", opts_init.ny);
  printf("nz = %d\n", opts_init.nz);
*/
  prtcls = factory<double>(
    backend,
    opts_init
  );

  std::vector<double> vth(opts_init.nx * m1(opts_init.ny) * opts_init.nz, 300.);
  std::vector<double> vrhod(opts_init.nx * m1(opts_init.ny) * opts_init.nz, 1.225);
  std::vector<double> vrv(opts_init.nx * m1(opts_init.ny) * opts_init.nz, 0.01);
  std::vector<double> vCxm((opts_init.nx + 3) * m1(opts_init.ny) * opts_init.nz, -1);
  std::vector<double> vCxp((opts_init.nx + 3) * m1(opts_init.ny) * opts_init.nz, 1);
  std::vector<double> vCy((opts_init.nx + 2) * (m1(opts_init.ny+1)) * opts_init.nz, 0);
  std::vector<double> vCz((opts_init.nx + 2) * m1(opts_init.ny) * (opts_init.nz+1), 0);

/*
  double pth[] = {300., 300., 300., 300.};
  double prhod[] = {1.225, 1.225, 1.225, 1.225};
  double prv[] = {.01, 0.01, 0.01, 0.01};
  double pCxm[] = {-1, -1, -1, -1, -1, -1, -1};
  double pCxp[] = {1, 1, 1, 1, 1, 1, 1};
  double pCz[] = {0., 0., 0., 0., 0., 0., 0., 0., 0., 0., 0. ,0.};
  double pCy[] = {0., 0., 0., 0., 0., 0., 0., 0., 0., 0., 0. ,0.};
*/
  //long int strides[] = {sizeof(double)};
  long int strides[] = {0, 1, 1};
  long int xstrides[] = {0, 1, 1};
  long int ystrides[] = {0, 1, 1};
  long int zstrides[] = {0, 1, 1};

/*
  arrinfo_t<double> th(pth, strides);
  arrinfo_t<double> rhod(prhod, strides);
  arrinfo_t<double> rv(prv, strides);
  arrinfo_t<double> Cx( dir ? pCxm : pCxp, xstrides);
  arrinfo_t<double> Cz(pCz, ystrides);
  arrinfo_t<double> Cy(pCy, ystrides);
*/

  arrinfo_t<double> th(vth.data(), strides);
  arrinfo_t<double> rhod(vrhod.data(), strides);
  arrinfo_t<double> rv(vrv.data(), strides);
  arrinfo_t<double> Cx( dir ? vCxm.data() : vCxp.data(), xstrides);
  arrinfo_t<double> Cz(vCz.data(), ystrides);
  arrinfo_t<double> Cy(vCy.data(), ystrides);

  if(ndims==1)
    prtcls->init(th,rv,rhod, arrinfo_t<double>(), Cx);
  else if(ndims==2)
    prtcls->init(th,rv, rhod, arrinfo_t<double>(), Cx, arrinfo_t<double>(), Cz);
  else if(ndims==3)
    prtcls->init(th,rv, rhod, arrinfo_t<double>(), Cx, Cy, Cz);

  opts_t<double> opts;
  opts.adve = 0;
  opts.sedi = 0;
  opts.cond = 0;
  opts.coal = 1;
//  opts.chem = 0;


  prtcls->diag_all();
  prtcls->diag_sd_conc();
  double *out = prtcls->outbuf();
/*
  printf("---sd_conc init---\n");
  printf("%d: %lf %lf %lf %lf\n",rank, out[0], out[1], out[2], out[3]);
*/
  MPI_Barrier(MPI_COMM_WORLD);
  
  for(int i=0;i<70;++i)
  {
//    if(rank==0)
    std::cerr << "pure coal step no " << i << std::endl;
    two_step(prtcls,th,rhod,rv,Cx, ndims==2 ? arrinfo_t<double>() : Cy, Cz, opts);
//   //   MPI_Barrier(MPI_COMM_WORLD);
  //  if(rank==1)
   //   two_step(prtcls,th,rhod,rv,opts);
//   // MPI_Barrier(MPI_COMM_WORLD);
    std::cerr << "pure coal DONE step no " << i << std::endl;
  }

  prtcls->diag_all();
  prtcls->diag_sd_conc();
  out = prtcls->outbuf();

  printf("---sd_conc po coal---\n");
  // sequential output
  for(int i = 0; i < s; ++i)
  {
    if(i == rank)
    {
      printf("%d:",rank);
      for(int j = 0; j < opts_init.nx; ++j)
        printf(" %lf", out[j]);
      printf("\n");
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }

  std::vector<double> sd_conc_global_post_coal(nx_total * opts_init.nz * m1(opts_init.ny));
  std::vector<double> sd_conc_global_post_adve(nx_total * opts_init.nz * m1(opts_init.ny));
  
  std::vector<int> recvcount(size), displs(size);
  std::iota(recvcount.begin(), recvcount.end(), nx_min); 
  std::transform(recvcount.begin(), recvcount.end(), recvcount.begin(), [](auto& c){return c*opts_init.nz * m1(opts_init.ny);}
  std::exclusive_scan(recvcount.begin(), recvcount.end(), displs.begin(), 0);
  MPI_Gatherv(out, opts_init.nx * opts_init.nz * m1(opts_init.ny), MPI_DOUBLE, sd_conc_global_post_coal.data(), recvcount.data(), displs.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);


  opts.coal = 0;
  opts.adve = 1;
  for(int i=0; i<nx_total; ++i)
  {
    std::cerr << "pure adve step no " << i << std::endl;
    two_step(prtcls,th,rhod,rv,Cx, ndims==2 ? arrinfo_t<double>() : Cy, Cz, opts);
    std::cerr << "pure adve DONE step no " << i << std::endl;
  }
  prtcls->diag_all();
  prtcls->diag_sd_conc();
  out = prtcls->outbuf();

  printf("---sd_conc po adve---\n");
  for(int i = 0; i < s; ++i)
  {
    if(i == rank)
    {
      printf("%d:",rank);
      for(int j = 0; j < opts_init.nx; ++j)
        printf(" %lf", out[j]);
      printf("\n");
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }

  MPI_Gatherv(out, opts_init.nx * opts_init.nz * m1(opts_init.ny), MPI_DOUBLE, sd_conc_global_post_adve.data(), recvcount.data(), displs.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);

  if(rank==0)
  {
    if(sd_conc_global_post_coal != sd_conc_global_post_adve)
      throw std::runtime_error("error in advection\n");
  }
}

int main(int argc, char *argv[]){
  std::cout << "hostname: ";
  system("/bin/hostname");

// parsing arguments

  int n_devices;
  int cuda;
//  int nsecs, tfnd;
//
//  nsecs = 0;
//  tfnd = 0;
//  flags = 0;
  while ((opt = getopt(argc, argv, "cd:")) != -1) {
      switch (opt) {
      case 'd':
          nsecs = atoi(optarg);
          break;
      case 'c':
          cuda = atoi(optarg);
          break;
      default: /* '?' */
          fprintf(stderr, "Usage: %s -d number_of_devices_per_node\n",
                  argv[0]);
          exit(EXIT_FAILURE);
      }
  }


  int provided_thread_lvl;
  MPI_Init_thread(nullptr, nullptr, MPI_THREAD_MULTIPLE, &provided_thread_lvl);
  printf("provided thread lvl: %d\n", provided_thread_lvl);

  std::vector<backend_t> backends = {backend_t(serial), backend_t(OpenMP)};
  if(cuda)
    backends.push_back(backend_t(CUDA));

  for(auto back: backends)
  {
    // 1d doesnt work with MPI
    // 2D
  MPI_Barrier(MPI_COMM_WORLD);
    test(back, 2, false, n_devices);
  MPI_Barrier(MPI_COMM_WORLD);
    test(back, 2, true, n_devices);
  MPI_Barrier(MPI_COMM_WORLD);
    // 3D
    test(back, 3, false, n_devices);
  MPI_Barrier(MPI_COMM_WORLD);
    test(back, 3, true, n_devices);
  MPI_Barrier(MPI_COMM_WORLD);
  }
  MPI_Finalize();
}
