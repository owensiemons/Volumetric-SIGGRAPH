#pragma once
#include "common.hpp"

class Shader {
public:
    Shader(const char* frag_path, const char* vert_path);

    void useProgram() {
        glUseProgram(_shader_program);
    }

    unsigned int getProgram() {
        return _shader_program;
    }
private:
    void init_shaders(unsigned int vert_shader, unsigned int frag_shader, const char* vert_src, const char* frag_src);
    unsigned int _shader_program;
};