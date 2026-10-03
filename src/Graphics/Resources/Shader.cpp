#include "Shader.h"

namespace Graphics 
{
Shader::Shader(const char* name_) {
	GLuint vertexModuleId = this->createShaderModule(GL_VERTEX_SHADER, std::format("{}{}/vertex.glsl", shaderFolderPath, name_)); 
	GLuint fragmentModuleId = this->createShaderModule(GL_FRAGMENT_SHADER,std::format("{}{}/fragment.glsl", shaderFolderPath, name_));
	this->linkModules(vertexModuleId, fragmentModuleId); 
	this->modelLocation = glGetUniformLocation(this->id, "model");
	this->alive = true; 
}

void Shader::destroy() { 
    glDeleteProgram(this->id);
}

void Shader::linkModules(GLuint vertexModuleId_, GLuint fragmentModuleId_) {
  	this->id = glCreateProgram();

   	glAttachShader(this->id, vertexModuleId_);
   	glAttachShader(this->id, fragmentModuleId_);

   	glLinkProgram(this->id);

   	GLint success;
   	glGetProgramiv(this->id, GL_LINK_STATUS, &success);

   	if (!success) {
      	GLint size;
       	glGetProgramiv(this->id, GL_INFO_LOG_LENGTH, &size);

       	std::string log(size, '\0');
       	glGetProgramInfoLog(this->id, size, nullptr, log.data());

       	throw std::runtime_error("Failed to link GL shader program.\n" + log);
   	}
	glDeleteShader(vertexModuleId_);
    glDeleteShader(fragmentModuleId_);
}



GLuint Shader::createShaderModule(const GLenum type_, std::string filePath_) {
	std::ifstream file;
	std::stringstream bufferedLines;
	std::string line; 

	file.open(filePath_);
	if (!file.is_open()) {
    	throw std::runtime_error("Failed to open shader file: " + filePath_);
	}
	while (std::getline(file, line)) {
		bufferedLines << line << "\n"; // Builds mega string with source code to pass into gpu
	}

	std::string shaderSource = bufferedLines.str();

	const char* src = shaderSource.c_str(); 
	const GLuint module = glCreateShader(type_);

    glShaderSource(module, 1, &src, nullptr);
    glCompileShader(module);

    GLint status;
    glGetShaderiv(module, GL_COMPILE_STATUS, &status);

    if (!status) {
        GLint size;
		glGetShaderiv(module, GL_INFO_LOG_LENGTH, &size);

    	std::string log(size, '\0');
    	glGetShaderInfoLog(module, size, nullptr, log.data());

     
		throw std::runtime_error("Failed to compile GL shader module.\n" + log);
    }
    return module;
}
}