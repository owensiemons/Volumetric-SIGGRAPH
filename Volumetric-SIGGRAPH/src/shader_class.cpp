#include "shader_class.hpp"

Shader::Shader(const char* frag_path, const char* vert_path) {
    std::string vertexCode;
    std::string fragmentCode;
    {
        std::ifstream vShaderFile;
        std::ifstream fShaderFile;
        // ensure ifstream objects can throw exceptions:
        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try
        {
            // open files
            vShaderFile.open(vert_path);
            fShaderFile.open(frag_path);
            std::stringstream vShaderStream, fShaderStream;
            // read file's buffer contents into streams
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            // close file handlers
            vShaderFile.close();
            fShaderFile.close();
            // convert stream into string
            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();
        }
        catch (std::ifstream::failure& e)
        {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
        }
    }

    unsigned int vertexShader   = glCreateShader(GL_VERTEX_SHADER);
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    _shader_program  = glCreateProgram();

    init_shaders(vertexShader, fragmentShader, vertexCode.c_str(), fragmentCode.c_str());
}

// LearnOpenGL vertex / frag shader boilerplate
void Shader::init_shaders(unsigned int vert_shader, unsigned int frag_shader, const char* vert_src, const char* frag_src) {
    int success;
    char infoLog[512];
    glShaderSource(vert_shader, 1, &vert_src, NULL);
    glCompileShader(vert_shader);
    glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vert_shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    // fragment shader
    glShaderSource(frag_shader, 1, &frag_src, NULL);
    glCompileShader(frag_shader);
    // check for shader compile errors
    glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(frag_shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }


    // link shaders
    glAttachShader(_shader_program, vert_shader);
    glAttachShader(_shader_program, frag_shader);
    glLinkProgram(_shader_program);
    // check for linking errors
    glGetProgramiv(_shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(_shader_program, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);
}