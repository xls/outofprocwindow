/*
* This is a quick hack to test opengl's ability to clear resources upon process termination
* */
#include "stdafx.h"
#include "glad.h"

#define ELPP_STL_LOGGING
#define ELPP_FEATURE_ALL
#define ELPP_THREAD_SAFE
#define ELPP_DEFAULT_LOG_FILE "logs\\outofproc.log"

#include "easylogging++.h"

#include "OutofProcWindow.h"

#include <chrono>
#include <format>
#include <string>
#include <string_view>
#include <iostream>
#include <vector>
#include <random>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "options.h"


#pragma comment(lib,"opengl32.lib")

INITIALIZE_EASYLOGGINGPP

// ------------------------------
// Global Variables and win32 bs:

#define MAX_LOADSTRING 100

bool				bClosing = false;
HINSTANCE			hInst;
TCHAR				szTitle[MAX_LOADSTRING];
TCHAR				szWindowClass[MAX_LOADSTRING];
TCHAR				szChildClass[MAX_LOADSTRING];
HBRUSH				_backColor = NULL;
HGLRC				_glRenderContext = 0;
HDC					_glDC = 0;
HWND				_masterHwnd = 0;
BOOL				IsMaster() { return _masterHwnd == 0; }
std::vector<HWND>	siblingsHwnd;
HWND				_currentHwnd;
std::vector<PROCESS_INFORMATION> _vProcesses;
std::string			_instance_name;
int					_instance_id = -1;

// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
ATOM				MyRegisterChildClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);


// ------------------------------
// Default Configuration options
int kMax_num_process_count		= 3;
int kRespawn_time_in_seconds	= 2;
int kNum_Textures				= 10;
const int kMaxNum_Textures		= 100;
size_t kTexture_width			= 4096;
size_t kTexture_size			= kTexture_width * kTexture_width * 4;


// ------------------------------
// Shaders

const char* tshader = R"SHADER(
#version 330 core

// Interpolated values from the vertex shaders
in vec2 UV;

// Ouput data
out vec3 color;

// Values that stay constant for the whole mesh.
uniform sampler2D myTextureSampler;

void main() {

	// Output color = color of the texture at the specified UV
	color = texture(myTextureSampler, UV).rgb;
})SHADER";


const char* vshader = R"SHADER(
#version 330 core


// Input vertex data, different for all executions of this shader.
layout(location = 0) in vec3 vertexPosition_modelspace;
layout(location = 1) in vec2 vertexUV;

// Output data ; will be interpolated for each fragment.
out vec2 UV;

// Values that stay constant for the whole mesh.
uniform mat4 MVP;

void main() {

	// Output position of the vertex, in clip space : MVP * position
	gl_Position = MVP * vec4(vertexPosition_modelspace, 1);

	// UV of the vertex. No special space for this one.
	UV = vertexUV;
}
)SHADER";

// ------------------------------
// gl stuff

GLuint LoadShaders() {

	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

	GLint Result = GL_FALSE;
	int InfoLogLength;
	
	glShaderSource(VertexShaderID, 1, &vshader, NULL);
	glCompileShader(VertexShaderID);

	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0) {
		std::vector<char> VertexShaderErrorMessage(InfoLogLength + 1);
		glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, &VertexShaderErrorMessage[0]);
		LOG(ERROR) << "[" << _instance_name << "] " << &VertexShaderErrorMessage[0];
	}

	glShaderSource(FragmentShaderID, 1, &tshader, NULL);
	glCompileShader(FragmentShaderID);


	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0) {
		std::vector<char> FragmentShaderErrorMessage(InfoLogLength + 1);
		glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, &FragmentShaderErrorMessage[0]);
		LOG(ERROR) << "[" << _instance_name << "] " << &FragmentShaderErrorMessage[0];
	}

	// Link 

	GLuint ProgramID = glCreateProgram();
	glAttachShader(ProgramID, VertexShaderID);
	glAttachShader(ProgramID, FragmentShaderID);
	glLinkProgram(ProgramID);

	// Check the program
	glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
	glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0) {
		std::vector<char> ProgramErrorMessage(InfoLogLength + 1);
		glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
		LOG(ERROR) << "[" << _instance_name << "] " << &ProgramErrorMessage[0];
	}

	glDetachShader(ProgramID, VertexShaderID);
	glDetachShader(ProgramID, FragmentShaderID);

	glDeleteShader(VertexShaderID);
	glDeleteShader(FragmentShaderID);

	return ProgramID;
}

// ------------------------------
// cube generated from blender

static const GLfloat g_vertex_buffer_data[] = {
	-1.0f,-1.0f,-1.0f, // triangle 1 : begin
	-1.0f,-1.0f, 1.0f,
	-1.0f, 1.0f, 1.0f, // triangle 1 : end
	1.0f, 1.0f,-1.0f, // triangle 2 : begin
	-1.0f,-1.0f,-1.0f,
	-1.0f, 1.0f,-1.0f, // triangle 2 : end
	1.0f,-1.0f, 1.0f,
	-1.0f,-1.0f,-1.0f,
	1.0f,-1.0f,-1.0f,
	1.0f, 1.0f,-1.0f,
	1.0f,-1.0f,-1.0f,
	-1.0f,-1.0f,-1.0f,
	-1.0f,-1.0f,-1.0f,
	-1.0f, 1.0f, 1.0f,
	-1.0f, 1.0f,-1.0f,
	1.0f,-1.0f, 1.0f,
	-1.0f,-1.0f, 1.0f,
	-1.0f,-1.0f,-1.0f,
	-1.0f, 1.0f, 1.0f,
	-1.0f,-1.0f, 1.0f,
	1.0f,-1.0f, 1.0f,
	1.0f, 1.0f, 1.0f,
	1.0f,-1.0f,-1.0f,
	1.0f, 1.0f,-1.0f,
	1.0f,-1.0f,-1.0f,
	1.0f, 1.0f, 1.0f,
	1.0f,-1.0f, 1.0f,
	1.0f, 1.0f, 1.0f,
	1.0f, 1.0f,-1.0f,
	-1.0f, 1.0f,-1.0f,
	1.0f, 1.0f, 1.0f,
	-1.0f, 1.0f,-1.0f,
	-1.0f, 1.0f, 1.0f,
	1.0f, 1.0f, 1.0f,
	-1.0f, 1.0f, 1.0f,
	1.0f,-1.0f, 1.0f
};
static const GLfloat g_color_buffer_data[] = {
	0.583f,  0.771f,  0.014f,
	0.609f,  0.115f,  0.436f,
	0.327f,  0.483f,  0.844f,
	0.822f,  0.569f,  0.201f,
	0.435f,  0.602f,  0.223f,
	0.310f,  0.747f,  0.185f,
	0.597f,  0.770f,  0.761f,
	0.559f,  0.436f,  0.730f,
	0.359f,  0.583f,  0.152f,
	0.483f,  0.596f,  0.789f,
	0.559f,  0.861f,  0.639f,
	0.195f,  0.548f,  0.859f,
	0.014f,  0.184f,  0.576f,
	0.771f,  0.328f,  0.970f,
	0.406f,  0.615f,  0.116f,
	0.676f,  0.977f,  0.133f,
	0.971f,  0.572f,  0.833f,
	0.140f,  0.616f,  0.489f,
	0.997f,  0.513f,  0.064f,
	0.945f,  0.719f,  0.592f,
	0.543f,  0.021f,  0.978f,
	0.279f,  0.317f,  0.505f,
	0.167f,  0.620f,  0.077f,
	0.347f,  0.857f,  0.137f,
	0.055f,  0.953f,  0.042f,
	0.714f,  0.505f,  0.345f,
	0.783f,  0.290f,  0.734f,
	0.722f,  0.645f,  0.174f,
	0.302f,  0.455f,  0.848f,
	0.225f,  0.587f,  0.040f,
	0.517f,  0.713f,  0.338f,
	0.053f,  0.959f,  0.120f,
	0.393f,  0.621f,  0.362f,
	0.673f,  0.211f,  0.457f,
	0.820f,  0.883f,  0.371f,
	0.982f,  0.099f,  0.879f
};
static const GLfloat g_uv_buffer_data[] = {
	0.000059f, 1.0f - 0.000004f,
	0.000103f, 1.0f - 0.336048f,
	0.335973f, 1.0f - 0.335903f,
	1.000023f, 1.0f - 0.000013f,
	0.667979f, 1.0f - 0.335851f,
	0.999958f, 1.0f - 0.336064f,
	0.667979f, 1.0f - 0.335851f,
	0.336024f, 1.0f - 0.671877f,
	0.667969f, 1.0f - 0.671889f,
	1.000023f, 1.0f - 0.000013f,
	0.668104f, 1.0f - 0.000013f,
	0.667979f, 1.0f - 0.335851f,
	0.000059f, 1.0f - 0.000004f,
	0.335973f, 1.0f - 0.335903f,
	0.336098f, 1.0f - 0.000071f,
	0.667979f, 1.0f - 0.335851f,
	0.335973f, 1.0f - 0.335903f,
	0.336024f, 1.0f - 0.671877f,
	1.000004f, 1.0f - 0.671847f,
	0.999958f, 1.0f - 0.336064f,
	0.667979f, 1.0f - 0.335851f,
	0.668104f, 1.0f - 0.000013f,
	0.335973f, 1.0f - 0.335903f,
	0.667979f, 1.0f - 0.335851f,
	0.335973f, 1.0f - 0.335903f,
	0.668104f, 1.0f - 0.000013f,
	0.336098f, 1.0f - 0.000071f,
	0.000103f, 1.0f - 0.336048f,
	0.000004f, 1.0f - 0.671870f,
	0.336024f, 1.0f - 0.671877f,
	0.000103f, 1.0f - 0.336048f,
	0.336024f, 1.0f - 0.671877f,
	0.335973f, 1.0f - 0.335903f,
	0.667969f, 1.0f - 0.671889f,
	1.000004f, 1.0f - 0.671847f,
	0.667979f, 1.0f - 0.335851f
};





// ------------------------------
// helper stuff

BOOL GetMasterWindow(HWND* hwnd)
{
	if(hwnd == NULL) 
		return false;

	*hwnd = NULL;

	struct finder
	{
		static BOOL CALLBACK EnumMasterWindowsProc(
			  _In_  HWND hwnd,
			  _In_  LPARAM lParam
			)
			{
				TCHAR name[255];
				::RealGetWindowClass(hwnd,name,255);
				if(_tcscmp(name,szWindowClass) == 0)
				{
					*((HWND*)lParam) = hwnd;
					return false;
				}

				return true;
			}
	};

	::EnumWindows(&finder::EnumMasterWindowsProc,(LPARAM)hwnd);
	return *hwnd != 0;
}


size_t GetSiblings(HWND parent)
{
	struct finder
	{
		static BOOL CALLBACK EnumSiblingsProc(
		  _In_  HWND hwnd,
		  _In_  LPARAM lParam
		)
		{
			TCHAR name[255];
			::RealGetWindowClass(hwnd,name,255);
			if(_tcscmp(name,szChildClass) == 0)
			{
				siblingsHwnd.push_back(hwnd);
			}

			return true;
		}
	};
	
	siblingsHwnd.clear();
	::EnumChildWindows(parent,&finder::EnumSiblingsProc,NULL);
	return siblingsHwnd.size();
}

void* memset32(void* m, uint32_t val, size_t count)
{
	count /= 4;
	uint32_t* buf = static_cast<uint32_t*>(m);
	while (count--) *buf++ = val;
	return m;
}


// ------------------------------
// opengl scene variables

GLuint _programID;
GLuint g_vertexbuffer;
GLuint g_uvbuffer;
GLuint g_colorbuffer;
GLuint g_textures[kMaxNum_Textures];

int g_currentTexture = 0;

// ------------------------------
// opengl initialization and resource allocation (buffers and shaders)

BOOL InitGLContext(HWND hWnd)
{
	 // Initialize OpenGL
    PIXELFORMATDESCRIPTOR pixelFormatDescriptor =				        
    {
        sizeof(PIXELFORMATDESCRIPTOR),				// Size Of This Pixel Format Descriptor
        1,											// Version Number
        PFD_DRAW_TO_WINDOW |						// Format Must Support Window
        PFD_SUPPORT_OPENGL |						// Format Must Support OpenGL
        PFD_DOUBLEBUFFER |                          // Must Support Double Buffering
        PFD_SUPPORT_COMPOSITION,							
        PFD_TYPE_RGBA,								// Request An RGBA Format
        24,		     								// Select Our Color Depth
        0, 0, 0, 0, 0, 0,							// Color Bits Ignored
        0,											// No Alpha Buffer
        0,											// Shift Bit Ignored
        0,											// No Accumulation Buffer
        0, 0, 0, 0,									// Accumulation Bits Ignored
        16,											// 16Bit Z-Buffer (Depth Buffer)  
        16,											// 16Bit Stencil buffer
        0,											// No Auxiliary Buffer
        PFD_MAIN_PLANE,								// 4 Drawing Layer
        0,											// Reserved
        0, 0, 0										// Layer Masks Ignored
    };

    _glDC = ::GetDC(hWnd);
    if(_glDC == NULL)
    {
        return FALSE;
    }
    
    GLuint pixelFormat = ChoosePixelFormat(_glDC, &pixelFormatDescriptor);
    if(pixelFormat == 0)
    {
		LOG(ERROR) << "[" << _instance_name << "] " << "ChoosePixelFormat returned 0";
        return FALSE;
    }
	if (pixelFormat != 11)
	{
		LOG(WARNING) << "[" << _instance_name << "] " << "ChoosePixelFormat returned: " << pixelFormat;
	}
    if(FALSE == SetPixelFormat(_glDC, pixelFormat, &pixelFormatDescriptor))
    {
		LOG(ERROR) << "[" << _instance_name << "] " << "SetPixelFormat failed with pixelFormat: " << pixelFormat;
        return FALSE;
    }

	LOG(INFO) << "[" << _instance_name << "] " << "SetPixelFormat successful with pixelFormat: " << pixelFormat;
    _glRenderContext = wglCreateContext(_glDC);
	wglMakeCurrent(_glDC, _glRenderContext);
	GLVersion.major = 3;
	GLVersion.minor = 3;
	gladLoadGL();

	_programID = LoadShaders();
	glShadeModel(GL_SMOOTH);						
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);				
	glClearDepth(1.0f);									
	glDisable(GL_DEPTH_TEST);							
	glDepthFunc(GL_LEQUAL);								
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glEnable(GL_NORMALIZE);


	char* large_texture = new char[kTexture_size];
	
	
	glGenTextures(kNum_Textures, g_textures);
	//::ZeroMemory(large_texture, kTexture_size);
	std::mt19937 gen;
	gen.seed(static_cast<uint32_t>(time(nullptr)));
	for (auto t = 0; t < kNum_Textures; ++t)
	{
		unsigned val = (unsigned int)(gen()) | 0x000000ff;
		::memset32(large_texture, val, kTexture_size);
		glBindTexture(GL_TEXTURE_2D, g_textures[t]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)kTexture_width, (GLsizei)kTexture_width, 0, GL_RGBA, GL_UNSIGNED_BYTE, large_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		
	} // ignore freeing gl memory (lets see what driver does)
	LOG(INFO) << "[" << _instance_name << "] " << "Allocated " << ((kNum_Textures* kTexture_size)/(1024*1024)) << "mb of Textures.";

	GLuint VertexArrayID;
	glGenVertexArrays(1, &VertexArrayID);
	glBindVertexArray(VertexArrayID);


	
	// Generate 1 buffer, put the resulting identifier in vertexbuffer
	glGenBuffers(1, &g_vertexbuffer);
	// The following commands will talk about our 'vertexbuffer' buffer
	glBindBuffer(GL_ARRAY_BUFFER, g_vertexbuffer);
	// Give our vertices to OpenGL.
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertex_buffer_data), g_vertex_buffer_data, GL_STATIC_DRAW);

	
	// Generate 1 buffer, put the resulting identifier in vertexbuffer
	glGenBuffers(1, &g_uvbuffer);
	// The following commands will talk about our 'vertexbuffer' buffer
	glBindBuffer(GL_ARRAY_BUFFER, g_uvbuffer);
	// Give our vertices to OpenGL.
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_uv_buffer_data), g_uv_buffer_data, GL_STATIC_DRAW);
	
	glGenBuffers(1, &g_colorbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, g_colorbuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_color_buffer_data), g_color_buffer_data, GL_STATIC_DRAW);

	return TRUE;

}

// ------------------------------
// main scene render

void RenderScene()
{

	// some lame movement
	static float _time = 0;
	static float one = 0;
	static float sine = 0;
	bool up = false;

	if(_glRenderContext == 0) return;

	wglMakeCurrent(_glDC,_glRenderContext);
	_time +=1.0f;
	one+= up?0.01f:-0.01f;
	sine = (float)(sinf(_time /100.0f)+1.0)/2.0f;
	if(one > 1) {one = 1.0f;up = false;};
	if(one < 0) {one = 0.0f;up = true;};
	glClearColor(1-sine,one,sine,1.0f);
	

	/* render vbuffer */
	
	glm::mat4 Projection = glm::perspective(glm::radians(45.0f), (float)200 / (float)200, 0.1f, 100.0f);
	glm::mat4 View = glm::lookAt(
		glm::vec3(4, 3, 3), // camera
		glm::vec3(0, 0, 0), // origin
		glm::vec3(0, 1, 0)  // up 
	);

	
	glm::mat4 Model = glm::rotate(glm::mat4(1), (float)_time*0.02f, glm::vec3(1.0f, 1.00f, 1.00f)); //glm::mat4(1.0f);
	
	glm::mat4 mvp = Projection * View * Model;

	GLuint MatrixID = glGetUniformLocation(_programID, "MVP");


	glUniformMatrix4fv(MatrixID, 1, GL_FALSE, &mvp[0][0]);

	
	glEnable(GL_DEPTH_TEST);
	
	glDepthFunc(GL_LESS);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glUseProgram(_programID);
	glEnableVertexAttribArray(0);

	glBindTexture(GL_TEXTURE_2D, g_textures[g_currentTexture]);
	glBindBuffer(GL_ARRAY_BUFFER, g_vertexbuffer);
	glVertexAttribPointer(
		0,                  // vertex
		3,                  // size
		GL_FLOAT,           // type
		GL_FALSE,           // normalized
		0,                  // stride
		(void*)0            // array buffer offset
	);

	glBindBuffer(GL_ARRAY_BUFFER, g_uvbuffer);
	glVertexAttribPointer(
		1,                  // uv
		2,                  // size
		GL_FLOAT,           // type
		GL_FALSE,           // normalized
		0,                  // stride
		(void*)0            // array buffer offset
	);

	
	glDrawArrays(GL_TRIANGLES, 0, 12 * 3);
	glDisableVertexAttribArray(0);

	SwapBuffers(_glDC);


	g_currentTexture++;
	g_currentTexture = (++g_currentTexture) % kNum_Textures;
}
// ------------------------------
// process helpers

void StartNewProcess()
{
	if (bClosing) return;

	if ((int)_vProcesses.size() > kMax_num_process_count)
	{
		::TerminateProcess(_vProcesses.back().hProcess, 0);
		WaitForSingleObject(_vProcesses.back().hProcess, 1000);
		_vProcesses.pop_back();
	}

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));
	
	if (!CreateProcess(NULL,   // No module name (use command line)
		GetCommandLineA(),        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi)           // Pointer to PROCESS_INFORMATION structure
		)
	{
		LOG(ERROR) << "CreateProcess failed (" <<  GetLastError() << ")";
		return;
	}
	_vProcesses.push_back(pi);

}

void KillAllProcesses()
{
	for (auto p : _vProcesses)
	{
		::TerminateProcess(p.hProcess, 0);
		WaitForSingleObject(p.hProcess, 2000);
	}
	_vProcesses.clear();
}


// ------------------------------
// win32 window stuff

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_OUTOFPROCWINDOWS));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)NULL;//(COLOR_WINDOW+1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

ATOM MyRegisterChildClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = NULL;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)NULL;
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szChildClass;
	wcex.hIconSm = NULL;

	return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   HWND hWnd;

   hInst = hInstance; // Store instance handle in our global variable
   
   int cc = 3;
   int rc = (int)std::roundf((kMax_num_process_count / (float)cc) + 0.5f);
   
	if(kMax_num_process_count == 0)
	{
		hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW | WS_BORDER | WS_EX_LAYERED, CW_USEDEFAULT, 0, 640, 480 + 48 /* aprox titlebar*/, NULL, NULL, hInstance, NULL);
	}
	else if (IsMaster())
	{
		hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW | WS_BORDER | WS_EX_LAYERED, CW_USEDEFAULT, 0, cc * 200 + 18, rc * 200 + 48 /* aprox titlebar*/, NULL, NULL, hInstance, NULL);
	}
	else
	{

		int count = (int)GetSiblings(_masterHwnd);
		RECT rect;
		::GetClientRect(_masterHwnd,&rect);
		int rowc = (rect.right-rect.left)/200;
		int y = (int)(count /rowc);
		int x = (count - rowc * y);
		hWnd = CreateWindow(szChildClass, "", WS_CHILD|WS_BORDER|WS_EX_LAYERED,x*200, y*200, 200, 200, _masterHwnd, NULL, hInstance, NULL);
	}
   
	if (!hWnd)
	{
		return FALSE;
	}
	_currentHwnd = hWnd;
	::SetWindowLongPtr(hWnd, GWLP_USERDATA,siblingsHwnd.size()+1);
	if( !IsMaster() ||
		kMax_num_process_count == 0)
	{
		InitGLContext(hWnd);
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return TRUE;
}

// ------------------------------
// window proc

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{

	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{

	/*case WM_LBUTTONUP:
		{
			TCHAR msg[255];
			GetSiblings(_masterHwnd);
			if(IsMaster())
				_stprintf_s(msg,255,"Master Window siblings: %d",siblingsHwnd.size());
			else
				_stprintf_s(msg,255,"Click on sibling: %d",::GetWindowLong(hWnd,GWL_USERDATA));
		::MessageBox(hWnd,msg,"Information",MB_OK);
		}
		break;
		*/
	case WM_ERASEBKGND:
		if(IsMaster()) DefWindowProc(hWnd, message, wParam, lParam);
		if(kMax_num_process_count == 0) DefWindowProc(hWnd, message, wParam, lParam);
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		if(IsMaster() || kMax_num_process_count == 0)
		{
			if(_backColor == 0)
			{
				_backColor = ::CreateSolidBrush(RGB(0,0,0));
			}
			RECT rect;
		::GetClientRect(hWnd,&rect);
		::FillRect(hdc,&rect,_backColor);
		}
		EndPaint(hWnd, &ps);
		break;
	case WM_CLOSE:
		bClosing = true;
		return DefWindowProc(hWnd, message, wParam, lParam);
		break;
	case WM_DESTROY:
		if(_backColor != NULL)
		{
			::DeleteObject(_backColor);
			_backColor = NULL;
		}
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}
// ------------------------------
// Win Main Enrypoint


int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPTSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	int argc;
	char** argv;

	
	enum  optionIndex {
		OPT_HELP, OPT_NUM_PROCS, OPT_RESPAWN_SECOND, OPT_NUM_TEXTURES, OPT_TEXT_SIZE
	};
	
	argv = option::CommandLineToArgvWin(lpCmdLine,&argc);
	const option::Descriptor usage[] =
	{
		{ 0,  0, 0, option::Arg::Dummy, "USAGE: example [options]\n\n Options:" },
		{ OPT_HELP,					"h", "help", option::Arg::None,					"  -h,--help           Print usage and exit." },
		{ OPT_NUM_PROCS,			"c", "count", option::Arg::Numeric,				"  --count, -c         number of processes (default: 5)." },
		{ OPT_RESPAWN_SECOND,		"r", "respawn", option::Arg::Numeric,			"  --respawn, -r       respawn time in seconds (default: 3)." },
		{ OPT_NUM_TEXTURES,			"t", "textures", option::Arg::Numeric,			"  --textures, -t      texture count (default: 10)." },
		{ OPT_TEXT_SIZE,			"s", "size", option::Arg::Numeric,				"  --size, -s          texture width (default: 4096)." },
		{ 0, 0, 0, option::Arg::Dummy,"\nExamples:\n"
		"  example -h \n"
		"  example -r \"musthave\" -d -v -n 123 -s \"hello world\" -m=test -m=\"test 2\" -m \"test 3\"\n"
		"  example \n" },
		{ 0, 0, 0, option::Arg::End, 0 }	// terminating
	};
	
	option::Options opts((option::Descriptor*)usage);
	if (!opts.Parse((const char**)&argv[1], argc))	// skip first argument (module path)
	{
		LOG(INFO) << "* option error:" <<  opts.error_msg();
		LOG(INFO) << opts.cstr();
		return -1;
	}

	if (opts[OPT_HELP])
	{
		LOG(INFO) << opts.cstr();
		return -1;
	}

	if (opts[OPT_NUM_PROCS])
	{
		
		opts.GetArgument(OPT_NUM_PROCS, kMax_num_process_count);
		if (kMax_num_process_count > 20)
		{
			kMax_num_process_count = 20;
			LOG(INFO) << "invalid process count specified, max process count = 20";
		}
		
	}
	if (opts[OPT_RESPAWN_SECOND])
	{
		
		opts.GetArgument(OPT_RESPAWN_SECOND, kRespawn_time_in_seconds);
		if (kRespawn_time_in_seconds < 1)
		{
			kRespawn_time_in_seconds = 1;
			LOG(INFO) << "respawn time can't be less than 1 second";
		}
		if (kRespawn_time_in_seconds > 30)
		{
			kRespawn_time_in_seconds = 30;
			LOG(INFO) << "respawn time can't be more than 30 second";
		}
	}

	if (opts[OPT_NUM_TEXTURES])
	{
		opts.GetArgument(OPT_NUM_TEXTURES, kNum_Textures);
		if (kNum_Textures < 1)
		{
			LOG(INFO) << "texture count can't be less than 1";
			kNum_Textures = 1;
		}

		if (kNum_Textures > kMaxNum_Textures)
		{
			LOG(INFO) << "texture count can't be more than " << kMaxNum_Textures;
			kNum_Textures = kMaxNum_Textures;
		}
		
	}

	if (opts[OPT_TEXT_SIZE])
	{
		int tsize = 4096;
		opts.GetArgument(OPT_TEXT_SIZE, tsize);
		if (tsize > 4096)
		{
			LOG(INFO) << "texture size can't be more than 4096";
			tsize = 4096;
		}
		if (tsize < 256)
		{
			LOG(INFO) << "texture size can't be more less 256";
			tsize = 256;
		}
		kTexture_width = tsize;
		kTexture_size = kTexture_width * kTexture_width * 4;
	}

	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// TODO: Place code here.
	MSG msg;
	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_OUTOFPROCWINDOWS, szWindowClass, MAX_LOADSTRING);
	LoadString(hInstance, IDC_OUTOFPROCCHILD, szChildClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);
	MyRegisterChildClass(hInstance);

	GetMasterWindow(&_masterHwnd);
	_instance_id = (int)GetSiblings(_masterHwnd);
	if (_masterHwnd != 0)
	{
		_instance_name = std::format("{}", _instance_id);
	}
	else
	{
		_instance_name = "master";
	}
	LOG(INFO) << "[" << _instance_name << "] " << " started.";

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}
	std::chrono::steady_clock::time_point start_point = std::chrono::steady_clock::now();

	// Main message loop:
	while (1)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
			{
				break;
			}
		}
		::Sleep(10);
		if (IsMaster() && kMax_num_process_count > 0)
		{
			if ((int)_vProcesses.size() < kMax_num_process_count)
			{
				size_t oldCount = GetSiblings(_currentHwnd);
				StartNewProcess();
				while (GetSiblings(_currentHwnd) == oldCount && bClosing == false)
					::Sleep(20);
				size_t count = GetSiblings(_currentHwnd);
				size_t proc_count = _vProcesses.size();
				size_t vram = (proc_count * kTexture_size* (long)kNum_Textures) / (1024 * 1024);
				std::string text = std::format("OutOfProcWindow Number of child processes: {} vram: {} mb", _vProcesses.size(), vram);
				::SetWindowTextA(_currentHwnd, text.c_str());
			}

			std::chrono::steady_clock::time_point end_point = std::chrono::steady_clock::now();
			auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(end_point - start_point).count();
			if (elapsedTime > kRespawn_time_in_seconds)
			{
				KillAllProcesses();
				start_point = end_point;
			}
		}
		else
		{
			RenderScene();
		}
	}

	KillAllProcesses();

	LOG(INFO) << "[" << _instance_name << "] " << " exiting.";
	return (int)msg.wParam;
}