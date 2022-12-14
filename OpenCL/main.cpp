#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctime>
#include <fstream>
#include <string>
#include <string.h>
#include <CL/cl.h>

using namespace std;

const string PROGRAM_FILE = "kernel.cl";
const string KERNEL_FUNC = "calculateLengths";
const string OUTPUT_FILE = "result.txt";
const int LENGTHS_ARRAY_SIZE = 5000;

struct OpenCLProgram
{
    cl_program program = NULL;
    cl_context context = NULL;
    cl_command_queue queue = NULL;
    cl_device_id device_id;
    cl_platform_id cpPlatform;

    cl_int error;
    char* errorLog = nullptr;
    
    OpenCLProgram(const char **sourceCode)
    {
        error = clGetPlatformIDs(1, &cpPlatform, NULL);       
        error = clGetDeviceIDs(cpPlatform, CL_DEVICE_TYPE_GPU, 1, &device_id, NULL);
        context = clCreateContext(0, 1, &device_id, NULL, NULL, &error);
        queue = clCreateCommandQueue(context, device_id, 0, &error);
        program = clCreateProgramWithSource(context, 1, sourceCode, NULL, &error);
        error = clBuildProgram(program, 0, NULL, NULL , NULL, NULL);
        
        if (error != CL_SUCCESS)
        {
            size_t log_size;
            clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        
            errorLog = (char*)malloc(log_size);
            clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, log_size, errorLog, NULL);
        }
    }
    
    ~OpenCLProgram()
    {
        if (program != NULL)
            clReleaseProgram(program);
        if (queue != NULL)
            clReleaseCommandQueue(queue);
        if (context != NULL)
            clReleaseContext(context); 
        if (errorLog != nullptr)
            free(errorLog);
    }
};

void readFileToStr(string name, char **buffer)
{
    std::ifstream fin(name);
    std::string content((std::istreambuf_iterator<char>(fin)),
                        (std::istreambuf_iterator<char>()));

    *buffer = (char *)calloc(content.length() + 1, sizeof(char));
    strcpy(*buffer, content.data());
}

void calculateWithOpenCL(int firstNumber, int lastNumber, int *digitsWithLength)
{
    char *sourceCode;
    readFileToStr(PROGRAM_FILE, &sourceCode);

    OpenCLProgram clProgram((const char **)&sourceCode);

    if (clProgram.error != CL_SUCCESS)
    {
        printf("Build program error:\n\n%s\n", clProgram.errorLog);
        return;
    };

    int elemsPerThread = 100;

    size_t localSize = 64;
    size_t globalSize = ceil((float)(lastNumber - firstNumber + 1) / elemsPerThread / localSize) * localSize;

    cl_int error;
    cl_kernel kernel = clCreateKernel(clProgram.program, KERNEL_FUNC.c_str(), &error);

    cl_mem d_digitsWithLength = clCreateBuffer(clProgram.context, CL_MEM_USE_HOST_PTR, LENGTHS_ARRAY_SIZE * sizeof(int), digitsWithLength, NULL);

    error |= clSetKernelArg(kernel, 0, sizeof(int), &firstNumber);
    error |= clSetKernelArg(kernel, 1, sizeof(int), &lastNumber);
    error |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &d_digitsWithLength);
    error |= clSetKernelArg(kernel, 3, sizeof(int), &elemsPerThread);

    clEnqueueNDRangeKernel(clProgram.queue, kernel, 1, NULL, &globalSize, &localSize,
                           0, NULL, NULL);

    clFinish(clProgram.queue);

    if (error != CL_SUCCESS)
    {
        printf("Execute kernel error!\n", error);
        return;
    };

    clEnqueueReadBuffer(clProgram.queue, d_digitsWithLength, CL_TRUE, 0,
                        LENGTHS_ARRAY_SIZE * sizeof(int), digitsWithLength, 0, NULL, NULL);

    clReleaseKernel(kernel);
    clReleaseMemObject(d_digitsWithLength);
    free(sourceCode);
}

void calculateWithoutOpenCL(int firstNumber, int lastNumber, int *digitsWithLength)
{
    for (int i = firstNumber; i <= lastNumber; i++)
    {
        unsigned int n = i;
        int length = 1;

        while (n != 1)
        {
            n = (n % 2 == 0 ? n / 2 : 3 * n + 1);
            length++;
        }

        digitsWithLength[length]++;
    }
}

void writeResultsToFile(int *resWithOpenCL, int *resWithoutOpenCL, float timeWithOpenCL, float timeWithoutOpenCL)
{
    std::ofstream fout(OUTPUT_FILE);

    bool resultsEqual = true;
    for (int i = 0; i < LENGTHS_ARRAY_SIZE; i++)
    {
        if (resWithoutOpenCL[i] != resWithOpenCL[i])
        {
            resultsEqual = false;
            break;
        }
    }

    fout << "The results are " << (resultsEqual ? "" : "not ") << "equal\n";
    fout << "With OpenCL | without OpenCL\nTime: " << timeWithOpenCL << "c | " << timeWithoutOpenCL << "c\n";
    fout << "Length: amount of digits #1 | amount of digits #2\n";

    for (int i = 0; i < LENGTHS_ARRAY_SIZE; i++)
    {
        if (resWithOpenCL[i] != 0 && resWithoutOpenCL[i] != 0)
            fout << i << " : " << resWithOpenCL[i] << " | " << resWithoutOpenCL[i] << "\n";
    }

    fout << "The unspecified amounts of numbers for the length are 0\n";

    fout.close();
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("Required args: number#1, number#2\n");
        return -1;
    }

    long firstNumber = strtol(argv[1], NULL, 10);
    long secondNumber = strtol(argv[2], NULL, 10);

    if (firstNumber > secondNumber || firstNumber <= 1 || secondNumber <= 1)
    {
        printf("Condition: number#2 > number#1 > 1\n");
        return -1;
    }

    int *digitsWithLength1 = (int *)calloc(LENGTHS_ARRAY_SIZE, sizeof(int));
    int *digitsWithLength2 = (int *)calloc(LENGTHS_ARRAY_SIZE, sizeof(int));

    auto execute = [firstNumber, secondNumber](void (*func)(int, int, int *), int *digitsWithLength)
    {
        struct timespec start, end;
        clock_gettime(CLOCK_REALTIME, &start);
        func(firstNumber, secondNumber, digitsWithLength);
        clock_gettime(CLOCK_REALTIME, &end);
        return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1.0 / 1000000000;
    };

    printf("Calculation with OpenCL...\n");
    float time1 = execute(calculateWithOpenCL, digitsWithLength1);

    printf("Calculation without OpenCL...\n");
    float time2 = execute(calculateWithoutOpenCL, digitsWithLength2);

    printf("Writing results to file...\n");
    writeResultsToFile(digitsWithLength1, digitsWithLength2, time1, time2);

    free(digitsWithLength1);
    free(digitsWithLength2);
    return 0;
}
