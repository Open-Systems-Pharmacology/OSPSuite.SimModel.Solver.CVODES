#include "SimModelSolver_CVODES/SimModelSolver_CVODES.h"
#include <sstream>
#include <algorithm>
#include <math.h>
#include <nvector/nvector_openmp.h>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace std;

SimModelSolver_CVODES::SimModelSolver_CVODES(ISolverCaller* pSolverCaller, int problemSize, int numberOfSensitivityParameters)
   : SimModelSolverBase(pSolverCaller, problemSize, numberOfSensitivityParameters)
{
   _h0 = 0.0;
   _hMin = 0.0;
   _hMax = 60;
   _mxStep = 100000;

   _relTol_CVODE = 1e-9;
   _maxOrd = 5;
   _mxHNil = 10;
   _lmm = CV_BDF;

   _absTol_NV = NULL;
   _initialData = NULL;
   _solution = NULL;

   _cvodeMem = NULL;

   CVODES_UserData = new UserData();
   CVODES_UserData->Solver = this;

   _linearSolverMatrix = NULL;
   _linearSolver = NULL;

   _numThreads = 0;
}

int SimModelSolver_CVODES::getNumberOfThreads()
{
   if (_numThreads <= 0) //was not (properly) set by user
   {
#ifdef _OPENMP
      _numThreads = omp_get_max_threads() - 1; //default: set to max number of available threads minus one
      if (_numThreads == 0)
         _numThreads = 1;
#else
      _numThreads = 1;
#endif
   }

   return _numThreads;
}

void SimModelSolver_CVODES::setNumberOfThreads(int numberOfThreads)
{
   _numThreads = numberOfThreads;
}

SimModelSolver_CVODES::~SimModelSolver_CVODES()
{
   //clear memory
   this->Terminate();
   delete CVODES_UserData;
}

std::vector < OptionInfo > SimModelSolver_CVODES::GetSolverOptionsInfo()
{
   std::vector < OptionInfo > CVODE_Options;

   OptionInfo optionInfo;

   optionInfo.SetName("LMM");
   optionInfo.SetDescription("Linear Multistep Method");
   optionInfo.SetDefaultValue(CV_BDF);
   optionInfo.SetDataType(OptionInfo::SODT_ListOfValues);
   optionInfo.AddOptionValue(OptionValueInfo(CV_BDF, "BDF"));
   optionInfo.AddOptionValue(OptionValueInfo(CV_ADAMS, "ADAMS"));

   CVODE_Options.push_back(optionInfo);

   return CVODE_Options;
}

void SimModelSolver_CVODES::Init()
{
   const char* ERROR_SOURCE = "SimModelSolver_CVODES::Init";
   int i;

   try
   {
      //perform common solver initialization (base class init routine makes all common checks etc.)
      SimModelSolverBase::Init();

      //check if RHS function is set
      if (!_solverCaller->IsSet_ODERhsFunction())
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "ODE RHS function not set");

      // Initial data
      if (_initialData)
      {
#ifdef _OPENMP
         N_VDestroy_OpenMP(_initialData);
#else
         N_VDestroy_Serial(_initialData);
#endif
         _initialData = NULL;
      }
#ifdef _OPENMP
      _initialData = N_VNew_OpenMP(_problemSize, getNumberOfThreads());
#else
      _initialData = N_VNew_Serial(_problemSize);
#endif
      if (!_initialData)
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Cannot allocate memory for ODE initial data");

      //Set Initial Data and resize value vectors
      for (i = 0; i < _problemSize; i++)
         NV_Ith_S(_initialData, i) = _initialValues[i];

      //Get memory for solution vector 
#ifdef _OPENMP
      _solution = N_VNew_OpenMP(_problemSize, getNumberOfThreads());
#else
      _solution = N_VNew_Serial(_problemSize);
#endif
      if (!_solution)
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Cannot allocate memory for solution vector");

      //Instantiate CVODE solver and specify the solution method
      _cvodeMem = CVodeCreate(_lmm);
      if (_cvodeMem == NULL)
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Could not reserve memory for CVODE!");

      //Allocate memory and initialize CVODE
      int flag = CVodeInit(_cvodeMem, Rhs, _initialTime, _initialData);
      switch (flag)
      {
      case CV_SUCCESS:
         break;
      case CV_MEM_NULL:
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "The cvode memory block was not initialized through a previous call to CVodeCreate");
      case CV_MEM_FAIL:
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "A memory allocation request has failed.");
      case CV_ILL_INPUT:
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "An input argument to CVodeInit has an illegal value.");
      default:
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeInit returned unexpected value.");
      }

      //fill solver options 
      this->FillSolverOptions();

      //---- create CVODE linear solver (band or dense)
      if (_solverCaller->UseBandLinearSolver())
      {
         _linearSolverMatrix = SUNBandMatrix(_problemSize, _solverCaller->GetUpperHalfBandWidth(), _solverCaller->GetLowerHalfBandWidth());
         if (_linearSolverMatrix)
            _linearSolver = SUNLinSol_Band(_initialData, _linearSolverMatrix);
      }
      else
      {
         _linearSolverMatrix = SUNDenseMatrix(_problemSize, _problemSize);
         if (_linearSolverMatrix)
            _linearSolver = SUNLinSol_Dense(_initialData, _linearSolverMatrix);
      }

      if (!_linearSolverMatrix)
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Cannot allocate memory for the linear solver matrix");
      if (!_linearSolver)
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Cannot allocate memory for the linear solver");

      //Attach the matrix and linear solver
      flag = CVodeSetLinearSolver(_cvodeMem, _linearSolver, _linearSolverMatrix);
      if (flag != CVLS_SUCCESS)
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetLinearSolver failed.");

      //set jacobian function (if defined)
      if (_solverCaller->IsSet_ODEJacFunction())
         flag = CVodeSetJacFn(_cvodeMem, CVODE_JacFn);
      else
         flag = CVodeSetJacFn(_cvodeMem, NULL);

      if (flag != CVLS_SUCCESS)
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetJacFn failed.");

      setupSensitivityProblem();
   }
   catch (SimModelSolverErrorData& ED)
   {
      this->Terminate();
      throw ED;
   }
   catch (...)
   {
      this->Terminate();
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Unknown error occured during initialization of ODE system");
   }

   _initialized = true;
}

void SimModelSolver_CVODES::setupSensitivityProblem()
{
   const char* ERROR_SOURCE = "SimModelSolver_CVODES::setupSensitivityProblem";

   if (_numberOfSensitivityParameters == 0)
      return; //nothing to do

   int i;

   //---- initial sensitivity parameter values and scaling factors
   CVODES_UserData->SensitivityParameters = new double[_numberOfSensitivityParameters];
   CVODES_UserData->ScalingFactors = new double[_numberOfSensitivityParameters];
   if (!CVODES_UserData->SensitivityParameters || !CVODES_UserData->ScalingFactors)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Cannot allocate memory for user data");

   for (i = 0; i < _numberOfSensitivityParameters; i++)
   {
      CVODES_UserData->SensitivityParameters[i] = _sensitivityParametersInitialValues[i];

      //set sensitivity parameter scaling factor to:
      //   - fabs(initial parameter value), if it's !=0
      //   - 1, if it's <=0
      // (compare CVODES help, p. 94)
      if (_sensitivityParametersInitialValues[i] != 0.0)
      {
         CVODES_UserData->ScalingFactors[i] = fabs(_sensitivityParametersInitialValues[i]);
      }
      else
      {
         CVODES_UserData->ScalingFactors[i] = 1.0;
      }
   }

   //create matrix for storing of the sensitivity values dy_i/dp_j
#ifdef _OPENMP
   _sensitivityValues = N_VCloneVectorArray_OpenMP(_numberOfSensitivityParameters, _initialData);
#else
   _sensitivityValues = N_VCloneVectorArray_Serial(_numberOfSensitivityParameters, _initialData);
#endif
   if (_sensitivityValues == NULL)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Cannot allocate memory for ODE sensitivities initial data vector");

   //set initial values of sensitivities (dy_i/dp_j(0)) to zero
   for (i = 0; i < _numberOfSensitivityParameters; i++)
      N_VConst(0.0, _sensitivityValues[i]);

   //ODE RHS Sensitivity function provided by caller. If not set, pass NULL to the sensitivity init function
   CVSensRhs1Fn sensitivityRHS_Function = _solverCaller->IsSet_ODESensitivityRhsFunction() ? CVODE_SensitivityRhsFunction : NULL;

   //----- main sensitivity initialization routine
   //
   //Notes: 
   //
   //At the moment, always use CV STAGGERED for sensitivity solution method. 
   //Might be defined as user option in the future
   //
   //In the CV STAGGERED approach, the correction step for the sensitivity variables takes place at the same time 
   //for all sensitivity equations, but only after the correction of the state variables has converged and the
   //state variables have passed the local error test
   if (CVodeSensInit1(_cvodeMem, _numberOfSensitivityParameters, CV_STAGGERED, sensitivityRHS_Function, _sensitivityValues) != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSensInit1 failed");

   //When CVodeSensEEtolerances is called, cvodes will estimate tolerances for sensitivity
   //variables based on the tolerances supplied for states variables and the scaling factors of sensitivity parameters
   if (CVodeSensEEtolerances(_cvodeMem) != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSensEEtolerances failed");

   //The function CVodeSetSensErrCon specifies the error control strategy for sensitivity variables.
   //2nd argument specifies whether sensitivity variables are to be included (TRUE) or not(FALSE) in the error control mechanism.
   //Default CVODES value is false
   //Might be defined as user option in the future
   if (CVodeSetSensErrCon(_cvodeMem, false) != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetSensErrCon failed");

   //CVodeSetSensParams(cvode mem, p, pbar, plist) specifies problem parameter information for sensitivity calculations.
   //
   //	p is a pointer to the array of real problem parameters used to evaluate f(t, y, p).
   //  If non - NULL, p must point to a field in the user's data structure user_data passed to the RHS function
    //
   //  pbar is an array of NO_OF_SENSITIVITIES positive scaling factors.
   //	
   //  plist is an array of NO_OF_SENSITIVITIES indices to specify which components p[i] to use
   if (CVodeSetSensParams(_cvodeMem, CVODES_UserData->SensitivityParameters, CVODES_UserData->ScalingFactors, NULL) != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetSensParams failed");
}

int SimModelSolver_CVODES::PerformSolverStep(double tout, double* y, double** yS, double& tret, STEP_MODE step_mode)
{
   const char* ERROR_SOURCE = "SimModelSolver_CVODES::PerformSolverStep";

   int iResultflag;
   //check the solver was initialized
   if (!_initialized)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Solver was not initialized");

   if (step_mode == SINGLE)
   {
      // perform next solver step
      iResultflag = CVode(_cvodeMem, tout, _solution, &tret, CV_ONE_STEP);

   	_step++;
      if (_mxStep != 0 && _step > _mxStep)
         return CV_TOO_MUCH_WORK;

      // After a single internal step return to the caller
      if (iResultflag == CV_SUCCESS && tret < tout)
         return CV_SUCCESS;

      // After tout has been reached, call CVode with CV_NORMAL to ensure consistent _cvodeMem
      // it returns early after adjusting tret
      if (iResultflag == CV_SUCCESS)
      {
         _step = 0;
         iResultflag = CVode(_cvodeMem, tout, _solution, &tret, CV_NORMAL);
      }
   }
   else
   {
      iResultflag = CVode(_cvodeMem, tout, _solution, &tret, CV_NORMAL);
   }


   //copy new solution vector
#ifdef _OPENMP
   double* _SolutionData = NV_DATA_OMP(_solution);
#else
   double* _SolutionData = NV_DATA_S(_solution);
#endif

   int i;
   for (i = 0; i < _problemSize; i++)
      y[i] = _SolutionData[i];

   //if no sensitivity calculation is required or if CVode call was not successful - return
   if ((iResultflag != CV_SUCCESS) || (_numberOfSensitivityParameters == 0))
      return iResultflag;

   iResultflag = CVodeGetSens(_cvodeMem, &tret, _sensitivityValues);

   //copy sensitivity values 
   //at the end; yS[i][j]=dy_i/dp_j
   for (int j = 0; j < _numberOfSensitivityParameters; j++)
   {
#ifdef _OPENMP
      double* data = NV_DATA_OMP(_sensitivityValues[j]);
#else
      double* data = NV_DATA_S(_sensitivityValues[j]);
#endif

      for (i = 0; i < _problemSize; i++)
      {
         yS[i][j] = data[i];
      }
   }

   //return value of CVode call
   return iResultflag;
}

//TODO Reinitialization of sensitivity problem might be required as well!!! to be checked
int SimModelSolver_CVODES::ReInit(double t0, const vector < double >& y0)
{
   const char* ERROR_SOURCE = "SimModelSolver_CVODES::ReInit";

   int iResultFlag;

   //call basis class ReInit first (common part)
   iResultFlag = SimModelSolverBase::ReInit(t0, y0);
   if (iResultFlag != SimModelSolverErrorData::err_OK)
      return iResultFlag;

   //fill new initial data vector
   if (_initialData)
   {
#ifdef _OPENMP
      N_VDestroy_OpenMP(_initialData);
#else
      N_VDestroy_Serial(_initialData);
#endif
      _initialData = NULL;
   }
#ifdef _OPENMP
   _initialData = N_VNew_OpenMP(_problemSize, getNumberOfThreads());
#else
   _initialData = N_VNew_Serial(_problemSize);
#endif
   if (!_initialData)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Cannot allocate memory for ODE initial data vector");

   for (int i = 0; i < _problemSize; i++)
      NV_Ith_S(_initialData, i) = y0[i];


   //fill solver options
   this->FillSolverOptions();

   //call CVode ReInit routine
   iResultFlag = CVodeReInit(_cvodeMem, t0, _initialData);

   return iResultFlag;
}


void SimModelSolver_CVODES::Terminate()
{
   if (_solution)
   {
#ifdef _OPENMP
      N_VDestroy_OpenMP(_solution);
#else
      N_VDestroy_Serial(_solution);
#endif
      _solution = NULL;
   }

   if (_initialData)
   {
#ifdef _OPENMP
      N_VDestroy_OpenMP(_initialData);
#else
      N_VDestroy_Serial(_initialData);
#endif
      _initialData = NULL;
   }

   if (_absTol_NV)
   {
#ifdef _OPENMP
      N_VDestroy_OpenMP(_absTol_NV);
#else
      N_VDestroy_Serial(_absTol_NV);
#endif
      _absTol_NV = NULL;
   }

   if (_cvodeMem)
   {
      CVodeFree(&_cvodeMem);
      _cvodeMem = NULL;
   }

   if (_sensitivityValues && (_numberOfSensitivityParameters > 0))
   {
#ifdef _OPENMP
      N_VDestroyVectorArray_OpenMP(_sensitivityValues, _numberOfSensitivityParameters);
#else
      N_VDestroyVectorArray_Serial(_sensitivityValues, _numberOfSensitivityParameters);
#endif
      _sensitivityValues = NULL;
   }

   if (_linearSolver)
   {
      SUNLinSolFree(_linearSolver);
      _linearSolver = NULL;
   }

   if (_linearSolverMatrix)
   {
      SUNMatDestroy(_linearSolverMatrix);
      _linearSolverMatrix = NULL;
   }

   _initialized = false;
}

string SimModelSolver_CVODES::ToString(double dValue)
{
   std::ostringstream out;
   out.precision(16);
   out << dValue;

   return out.str();
}

SimModelSolverErrorData::errNumber SimModelSolver_CVODES::GetErrorNumberFromSolverReturnValue(int solverRetVal)
{
   switch (solverRetVal)
   {
   case CV_SUCCESS:
   case CV_TSTOP_RETURN:
   case CV_ROOT_RETURN:
      return SimModelSolverErrorData::err_OK;
   case CV_ILL_INPUT:
      return SimModelSolverErrorData::err_ILL_INPUT;
   case CV_TOO_MUCH_WORK:
      return SimModelSolverErrorData::err_TOO_MUCH_WORK;
   case CV_TOO_MUCH_ACC:
      return SimModelSolverErrorData::err_TOO_MUCH_ACC;
   case CV_ERR_FAILURE:
      return SimModelSolverErrorData::err_TEST_FAILURE;
   case CV_CONV_FAILURE:
      return SimModelSolverErrorData::err_CONV_FAILURE;
   }

   return SimModelSolverErrorData::err_FAILURE;
}

string SimModelSolver_CVODES::GetSolverErrMsg(int SolverRetVal)
{
   switch (SolverRetVal)
   {
   case CV_SUCCESS:
      return "CVode succeeded and no roots were found (CV_SUCCESS)";
   case CV_TOO_MUCH_WORK:
      return "The solver took " + ToString(_mxStep) +
         " internal steps but could not reach output time (TOO_MUCH_WORK)";
   case CV_TSTOP_RETURN:
      return "CVode succeeded by reaching the stopping point specified through the optional input function CVodeSetStopTime (CV_TSTOP_RETURN)";
   case CV_ROOT_RETURN:
      return "CVode succeeded and found one or more roots. If nrtfn > 1, call CVodeGetRootInfo to see which gi were found to have a root (CV_ROOT_RETURN)";
   case CV_MEM_NULL:
      return "The cvode mem argument was NULL (CV_MEM_NULL)";
   case CV_NO_MALLOC:
      return "The cvode memory was not allocated by a call to CVodeInit (CV_NO_MALLOC)";
   case CV_ILL_INPUT:
      return string("One of the inputs to CVode was illegal, or some other input to the \n") +
         "solver was either illegal or missing.\n" +
         "The latter category includes the following situations:\n" +
         "(a) The tolerances have not been set.\n" +
         "(b) A component of the error weight vector became zero during internal time-stepping.\n" +
         "(c) The linear solver initialization function (called by the user after calling CVodeCreate)\n" +
         "failed to set the linear solverspecific lsolve field in cvode mem.\n" +
         "(d) A root of one of the root functions was found both at a point t and also very near t.\n" +
         "(CV_ILL_INPUT)";
   case CV_TOO_CLOSE:
      return "The initial time t0 and the final time tout are too close to each other and the user did not specify an initial step size (CV_TOO_CLOSE)";
   case CV_TOO_MUCH_ACC:
      return "The solver could not satisfy the accuracy demanded by the user for some internal step (CV_TOO_MUCH_ACC)";
   case CV_ERR_FAILURE:
      return "Either error test failures occurred too many times during one internal time step, or with |h| = hmin (CV_ERR_FAILURE)";
   case CV_CONV_FAILURE:
      return "Either convergence test failures occurred too many times during one internal time step, or with |h| = hmin (CV_CONV_FAILURE)";
   case CV_LINIT_FAIL:
      return "The linear solver's initialization function failed (CV_LINIT_FAIL)";
   case CV_LSETUP_FAIL:
      return "The linear solver's setup function failed in an unrecoverable manner (CV_LSETUP_FAIL)";
   case CV_LSOLVE_FAIL:
      return "The linear solver's solve function failed in an unrecoverable manner (CV_LSOLVE_FAIL)";
   case CV_RHSFUNC_FAIL:
      return "The right-hand side function failed in an unrecoverable manner (CV_RHSFUNC_FAIL)";
   case CV_FIRST_RHSFUNC_ERR:
      return "The right-hand side function had a recoverable error at the first call (CV_FIRST_RHSFUNC_FAIL)";
   case CV_REPTD_RHSFUNC_ERR:
      return string("Convergence test failures occurred too many times due to repeated") +
         "recoverable errors in the right-hand side function. This flag " +
         "will also be returned if the rhs function had repeated " +
         "recoverable errors during the estimation of an initial step size " +
         "(CV_REPTD_RHSFUNC_ERR)";
   case CV_UNREC_RHSFUNC_ERR:
      return string("The right-hand function had a recoverable error, but no recovery ") +
         "was possible. This failure mode is rare, as it can occur only if the " +
         "right-hand side function fails recoverably after an error test failed" +
         "while at order one (CV_UNREC_RHSFUNC_ERR)";
   case CV_RTFUNC_FAIL:
      return "The rootfinding function failed (CV_RTFUNC_FAIL)";
   }

   return "Unknown Error";
}

void SimModelSolver_CVODES::SetOption(const std::string& name, double value)
{
   const char* ERROR_SOURCE = "SimModelSolver_CVODES::SetOption";

   string NameToUpper = name;
   transform(NameToUpper.begin(), NameToUpper.end(), NameToUpper.begin(),
      (int(*)(int)) toupper);

   if (NameToUpper == "LMM")
   {
      int iValue = (int)value + 1; //+1 because CVODE constants changed!!
      if ((iValue != CV_ADAMS) && (iValue != CV_BDF))
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Invalid value for CVODE solver option Lmm passed");
      _lmm = iValue;
   }
   else if (NameToUpper == "ITER")
   {
      //not used anymore. Keep option just for backward compatibility
   }
   else if (NameToUpper == "MAXORD")
   {
      int iValue = (int)value;
      if ((iValue < 1) || (iValue > 5))
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Invalid value for CVODE solver option MaxOrd passed");
      _maxOrd = iValue;
   }
   else if (NameToUpper == "MXHNIL")
   {
      int iValue = (int)value;
      _mxHNil = iValue;
   }
   else if (NameToUpper == "NUMBEROFTHREADS")
   {
      int iValue = (int)value;
      if (iValue < 1)
         throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Invalid value for CVODE solver option NumberOfThreads passed");
      setNumberOfThreads(iValue);
   }
   else
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Unknown CVODE solver option passed: " + name);

}

int SimModelSolver_CVODES::Rhs(realtype t, N_Vector y, N_Vector ydot,
   void* user_data)
{
   const char* ERROR_SOURCE = "SimModelSolver_CVODES::Rhs";

   //user_data MUST provide pointer to the instance of SimModelSolver_CVODES class
   //This instance cannot be accessed from Rhs because it's a static member

   UserData* userData = dynamic_cast<UserData*> ((UserData*)user_data);
   if (!userData)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Missing class instance pointer");

   //get pointer to the Solver caller instance and call the ODE RHS function
   ISolverCaller* pSolverCaller = userData->Solver->GetSolverCaller();

   //get new values of sensitivity parameters
   const double* p = userData->SensitivityParameters;

#ifdef _OPENMP
   Rhs_Return_Value RetVal = pSolverCaller->ODERhsFunction(t, NV_DATA_OMP(y), p, NV_DATA_OMP(ydot), NULL);
#else
   Rhs_Return_Value RetVal = pSolverCaller->ODERhsFunction(t, NV_DATA_S(y), p, NV_DATA_S(ydot), NULL);
#endif

   if (RetVal == RHS_OK)
      return 0;

   if (RetVal == RHS_RECOVERABLE_ERROR)
      return 1;

   return -1; //unrecoverable Error
}


int SimModelSolver_CVODES::CVODE_SensitivityRhsFunction(int Ns, realtype t, N_Vector y, N_Vector ydot, int iS,
   N_Vector yS, N_Vector ySdot, void* user_data,
   N_Vector tmp1, N_Vector tmp2)
{
   const char* ERROR_SOURCE = "SimModelSolver_CVODES::CVODE_SensitivityRhsFunction";

   //user_data MUST provide pointer to the instance of SimModelSolver_CVODES class
   //This instance cannot be accessed from Rhs because it's a static member

   UserData* userData = dynamic_cast<UserData*> ((UserData*)user_data);
   if (!userData)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Missing class instance pointer");

   //get pointer to the Solver caller instance and call ODE Sensitivity RHS function
   ISolverCaller* pSolverCaller = userData->Solver->GetSolverCaller();

#ifdef _OPENMP
   Sensitivity_Rhs_Return_Value RetVal = pSolverCaller->ODESensitivityRhsFunction(t, NV_DATA_OMP(y), NV_DATA_OMP(ydot), iS, NV_DATA_OMP(yS), NV_DATA_OMP(ySdot), NULL);
#else
   Sensitivity_Rhs_Return_Value RetVal = pSolverCaller->ODESensitivityRhsFunction(t, NV_DATA_S(y), NV_DATA_S(ydot), iS, NV_DATA_S(yS), NV_DATA_S(ySdot), NULL);
#endif

   if (RetVal == SENSITIVITY_RHS_OK)
      return 0;

   if (RetVal == SENSITIVITY_RHS_RECOVERABLE_ERROR)
      return 1;

   return -1; //unrecoverable Error
}

int SimModelSolver_CVODES::CVODE_JacFn(realtype t, N_Vector y, N_Vector fy, SUNMatrix J,
   void* user_data, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3)
{
   const char* ERROR_SOURCE = "SimModelSolver_CVODES::CVODE_JacFn";

   //user_data MUST provide pointer to the instance of SimModelSolver_CVODES class
   //This instance cannot be accessed from CVODE_JacFn because it's a static member

   UserData* userData = dynamic_cast<UserData*> ((UserData*)user_data);
   if (!userData)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Missing class instance pointer");

   //get pointer to the Solver caller instance and call the ODE RHS function
   ISolverCaller* pSolverCaller = userData->Solver->GetSolverCaller();

   //get new values of sensitivity parameters
   const double* p = userData->SensitivityParameters;

   Jacobian_Return_Value RetVal;

   if (pSolverCaller->IsSet_ODEJacFunction())
   {
      double** cols;
      if (pSolverCaller->UseBandLinearSolver())
         cols = SUNBandMatrix_Cols(J);
      else
         cols = SUNDenseMatrix_Cols(J);

#ifdef _OPENMP
      RetVal = pSolverCaller->ODEJacFunction(t, NV_DATA_OMP(y), p, NV_DATA_OMP(fy), cols, NULL);
#else
      RetVal = pSolverCaller->ODEJacFunction(t, NV_DATA_S(y), p, NV_DATA_S(fy), cols, NULL);
#endif
   }
   else
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Jacobian function not set");

   if (RetVal == JACOBIAN_OK)
      return 0;

   if (RetVal == JACOBIAN_RECOVERABLE_ERROR)
      return 1;

   return -1; //unrecoverable error
}

void SimModelSolver_CVODES::FillSolverOptions(void)
{
   const char* ERROR_SOURCE = "SimModelSolver_CVODES::FillSolverOptions";
   int flag;

   //relative tolerance
   _relTol_CVODE = _relTol;

   _step = 0;

   //absolute tolerance
   if (_absTol_NV)
   {
#ifdef _OPENMP
      N_VDestroy_OpenMP(_absTol_NV);
#else
      N_VDestroy_Serial(_absTol_NV);
#endif
   }

#ifdef _OPENMP
   _absTol_NV = N_VNew_OpenMP(_problemSize, getNumberOfThreads());
#else
   _absTol_NV = N_VNew_Serial(_problemSize);
#endif
   if (!_absTol_NV)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "Cannot allocate memory for absolute tolerances");

   for (int i = 0; i < _problemSize; i++)
      NV_Ith_S(_absTol_NV, i) = _absTol[i];

   //set solver tolerances
   flag = CVodeSVtolerances(_cvodeMem, _relTol_CVODE, _absTol_NV);
   switch (flag)
   {
   case CV_SUCCESS:
      break;
   case CV_MEM_NULL:
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE,
         "The cvode memory block was not initialized through a previous call to CVodeCreate");
   case CV_NO_MALLOC:
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE,
         "The allocation function CVodeInit has not been called.");
   case CV_ILL_INPUT:
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE,
         "The relative error tolerance was negative or the absolute tolerance had a negative component.");
   default:
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE,
         "CVodeSVtolerances returned unexpected value.");
   }

   //pass pointer to the actual class instance (casted to void *)
   flag = CVodeSetUserData(_cvodeMem, CVODES_UserData);
   if (flag != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetUserData failed.");

   //maximum order of the linear multistep method
   flag = CVodeSetMaxOrd(_cvodeMem, _maxOrd);
   if (flag != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE,
         "CVodeSetMaxOrder: The specified value is <= 0, or larger than its previous value.");

   //maximum number of steps to be taken by the solver in its attempt to reach the next output time.
   flag = CVodeSetMaxNumSteps(_cvodeMem, _mxStep);
   if (flag != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetMaxNumSteps failed.");

   //maximum number of messages issued by the solver warning that t + h = t on the next internal step.
   flag = CVodeSetMaxHnilWarns(_cvodeMem, _mxHNil);
   if (flag != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetMaxHnilWarns failed.");

   //specifies the initial step size.
   flag = CVodeSetInitStep(_cvodeMem, _h0);
   if (flag != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetInitStep failed.");

   //specifies the initial step size.
   flag = CVodeSetMaxStep(_cvodeMem, _hMax);
   if (flag != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetMaxStep failed.");

   //specifies the initial step size.
   flag = CVodeSetMinStep(_cvodeMem, _hMin);
   if (flag != CV_SUCCESS)
      throw SimModelSolverErrorData(SimModelSolverErrorData::err_FAILURE, ERROR_SOURCE, "CVodeSetMinStep failed.");
}

UserData::UserData()
{
   Solver = NULL;
   SensitivityParameters = NULL;
   ScalingFactors = NULL;
}

UserData::~UserData()
{
   if (SensitivityParameters)
   {
      delete[] SensitivityParameters;
      SensitivityParameters = NULL;
   }
   if (ScalingFactors)
   {
      delete[] ScalingFactors;
      ScalingFactors = NULL;
   }
}

// DLL main export function
extern "C" CVODES_EXPORT SimModelSolverBase* GetSolverInterface(ISolverCaller* pSolverCaller, int problemSize, int numberOfSensitivityParameters)
{
   SimModelSolverBase* pSolver = new SimModelSolver_CVODES(pSolverCaller, problemSize, numberOfSensitivityParameters);
   return pSolver;
}
