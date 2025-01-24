#ifndef __SimModelSolver_CVODES_H_
#define __SimModelSolver_CVODES_H_

#ifdef _WINDOWS
#pragma warning(disable:4786)
#endif

#include "cvodes/cvodes.h"
#include "sunlinsol/sunlinsol_dense.h"
#include "sunlinsol/sunlinsol_band.h"
#include "nvector/nvector_serial.h"

#include "SimModelSolverBase/SimModelSolverBase.h"
#include "SimModelSolverBase/SimModelSolverErrorData.h"

#ifdef _WINDOWS
#define CVODES_EXPORT __declspec(dllexport)
#endif
#ifdef linux
#define CVODES_EXPORT 
#endif
#ifdef __APPLE__
#define CVODES_EXPORT 
#endif

class UserData;

class SimModelSolver_CVODES : public SimModelSolverBase
{
private:

	//type of linear multistep method to be used (ADAMS or BDF)
	int _lmm;

	int _maxOrd;

	int _mxHNil;

	//absolute tolerance
	N_Vector _absTol_NV;      
    
	//initial values of ODE system
	N_Vector _initialData;
	
	//solution vector
	N_Vector _solution;

	N_Vector * _sensitivityValues;

	//a pointer to CVODE problem memory
	void * _cvodeMem;

	//relative tolerance
	realtype _relTol_CVODE;

	// internal step counter
	long _step;

	//fill CVOde specific Solver options in datatypes required by CVODE
	void FillSolverOptions(void);

	//Call to Rhs function
	static int Rhs (realtype t, N_Vector y, N_Vector ydot, void * user_data);

	//Call to jacobian function for the dense linear solver
	static int CVODE_JacFn(realtype t, N_Vector y, N_Vector fy, SUNMatrix J, 
		                   void *user_data, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3);

	//Call to sensitivity RHS function
	static int CVODE_SensitivityRhsFunction(int Ns, realtype t, N_Vector y, N_Vector ydot, int iS, 
		                                    N_Vector yS, N_Vector ySdot, void *user_data,
		                                    N_Vector tmp1, N_Vector tmp2);

	std::string ToString (double dValue);

	void setupSensitivityProblem();

	SUNMatrix _linearSolverMatrix;
	SUNLinearSolver _linearSolver;

	//number of threads to be used for parallel execution (e.g. OpenMP - if enabled)
	int _numThreads;
	int getNumberOfThreads();
	void setNumberOfThreads(int numberOfThreads);

public:
	UserData * CVODES_UserData;

	CVODES_EXPORT SimModelSolver_CVODES(ISolverCaller * pSolverCaller, int problemSize, int numberOfSensitivityParameters);

	CVODES_EXPORT virtual ~SimModelSolver_CVODES ();

	//Get vector with information of "Non-Standard" solver options
	CVODES_EXPORT std::vector < OptionInfo > GetSolverOptionsInfo ();

	//-----------------------------------------------------------------------------------------------------
	//Solver dependent initialization routine
	//MUST be called before first call to PerformSolverStep after all solver properties are set
	//
	//Inherited class Init routine:
	// - MUST call SimModelSolverInterface::Init() first
	// - MUST set initialized status = true at the end (in case of success)
	//-----------------------------------------------------------------------------------------------------
	CVODES_EXPORT void Init ();

	//-----------------------------------------------------------------------------------------------------
	//Get solution of ODE/DDE system at the "next" timepoint.
	// - [IN] tout: next time at which a solution is required
	// - [OUT] tret: time point reached by solver:
	//                        - returns tout, if no error occured
	//                        - returns value < tout otherwise (e.g. max. no. of internal solver steps reached; 
	//                                                                                     no convergence etc.)
	// - [OUT] y: Solution vector at time tret
	// - [OUT] yS: Parameter sensitivities at time tret. yS[i][j]=dy_i/dp_j
	//Returns:
	// - 0 if successful
	// - positive value if a recoverable error occurred (e.g. max. no. of internal solver steps reached)
	// - negative value if an unrecoverable error occurred (e.g. illegal input)
	//-----------------------------------------------------------------------------------------------------
	CVODES_EXPORT int PerformSolverStep(double tout, double * y, double ** yS, double & tret, SimModelSolverBase::STEP_MODE step_mode);

	//-----------------------------------------------------------------------------------------------------
	//Reinitialize DE system (e.g. in case of bigger discontinuities)
	//New relative / absolute tolerance should be set by caller prior to ReInit (if required)
	// - [IN] t0: continue integration from this time point
	// - [IN] y0: new initial value at t0
	//Returns:
	// - 0 if successful
	// - positive value if a recoverable error occurred 
	// - negative value if an unrecoverable error occurred 
	//
	//Inherited class:
	// - MUST call SimModelSolverInterface::ReInit first
	// - MUST perform solver-dependent checks (if applies)
	//-----------------------------------------------------------------------------------------------------
	CVODES_EXPORT int ReInit (double t0, const std::vector < double > & y0);

	//Solver dependent clean up routine (clear memory etc.)
	CVODES_EXPORT void Terminate ();

	//Retrieve solver error message from any error code returned by PerformSolverStep or ReInit
	CVODES_EXPORT std::string GetSolverErrMsg (int SolverRetVal);

	CVODES_EXPORT void SetOption(const std::string & name, double value);

	CVODES_EXPORT SimModelSolverErrorData::errNumber GetErrorNumberFromSolverReturnValue(int solverRetVal);
};

class UserData
{
public:
	SimModelSolver_CVODES * Solver;
	double * SensitivityParameters;
	double * ScalingFactors;
	UserData();
	virtual ~UserData();
};

#endif