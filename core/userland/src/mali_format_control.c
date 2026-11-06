/* Independent GLES reference for AIR's constant-color RGBA32UI draw.
 * Offscreen 4x4 targets only; no display or storage writes. */
#include <OpenGLES/ES3/gl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct osmesa_context *OSMesaContext;
extern OSMesaContext OSMesaCreateContextAttribs(const int *, OSMesaContext);
extern unsigned char OSMesaMakeCurrent(OSMesaContext, void *, unsigned, int, int);
extern void OSMesaDestroyContext(OSMesaContext);

static GLuint
shader(GLenum kind, const char *source)
{
    GLuint object = glCreateShader(kind);
    glShaderSource(object, 1, &source, NULL);
    glCompileShader(object);
    GLint ok = 0;
    glGetShaderiv(object, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char why[2048];
        glGetShaderInfoLog(object, sizeof(why), NULL, why);
        printf("mali_format_control: shader compilation failed: %s\n", why);
        glDeleteShader(object);
        return 0;
    }
    return object;
}

static int
draw(GLenum internal, GLenum external, GLenum type, const char *name, int integer)
{
    const char *vertex = "#version 300 es\nlayout(location=0) in vec4 p; void main(){gl_Position=p;}\n";
    const char *fragment = integer
        ? "#version 300 es\nprecision highp float; precision highp int; layout(location=0) out uvec4 c;"
          "void main(){c=uvec4(0x928b847du,0xaea7a099u,0xcac3bcb5u,0xe6dfd8d1u);}\n"
        : "#version 300 es\nprecision highp float; layout(location=0) out vec4 c;"
          "void main(){c=vec4(0.25,0.5,0.75,1.0);}\n";
    GLuint vs = shader(GL_VERTEX_SHADER, vertex), fs = shader(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs) return 1;
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char why[2048];
        glGetProgramInfoLog(program, sizeof(why), NULL, why);
        printf("mali_format_control: program link failed: %s\n", why);
        return 1;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(program);
    GLuint texture, framebuffer, vbo;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, 4, 4, 0, external, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    GLenum complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("mali_format_control: %s framebuffer %#x\n", name, complete);
    if (complete != GL_FRAMEBUFFER_COMPLETE) return 1;
    const GLfloat quad[] = {-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void *)0);
    glEnableVertexAttribArray(0);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, 4, 4);
    const GLuint zeros_u[4] = {0};
    const GLfloat zeros_f[4] = {0};
    if (integer) glClearBufferuiv(GL_COLOR, 0, zeros_u);
    else glClearBufferfv(GL_COLOR, 0, zeros_f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glFinish();
    int failed = 0;
    for (int corner = 0; corner < 2; corner++) {
        union { uint32_t u[4]; float f[4]; uint8_t b[16]; } pixel = {0};
        glReadPixels(corner * 3, corner * 3, 1, 1, external, type, &pixel);
        int match = 1;
        const uint32_t expected_u[4] = {2458616957u,2930221209u,3401825461u,3873429713u};
        const float expected_f[4] = {0.25f,0.5f,0.75f,1};
        const uint8_t expected_b[4] = {64,128,191,255};
        printf("mali_format_control: %s at (%d,%d):", name, corner * 3, corner * 3);
        for (unsigned c = 0; c < 4; c++) {
            if (integer) {
                printf(" %u", pixel.u[c]);
                match &= pixel.u[c] == expected_u[c];
            } else if (type == GL_FLOAT) {
                printf(" %.9g", (double)pixel.f[c]);
                match &= fabsf(pixel.f[c] - expected_f[c]) < 0.000001f;
            } else {
                printf(" %u", pixel.b[c]);
                match &= pixel.b[c] == expected_b[c];
            }
        }
        GLenum error = glGetError();
        printf("; GL error %#x; %s\n", error, match && !error ? "MATCH" : "DIFFERENT");
        failed |= !match || error;
    }
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    return failed;
}

int main(void)
{
    setenv("OSMESA_SOFTWARE", "0", 1);
    const int attributes[] = {0x22, GL_RGBA, 0x33, 0x38, 0x36, 3, 0};
    OSMesaContext context = OSMesaCreateContextAttribs(attributes, NULL);
    uint8_t dummy[4 * 4 * 4] = {0};
    if (!context || !OSMesaMakeCurrent(context, dummy, GL_UNSIGNED_BYTE, 4, 4)) return 2;
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    printf("mali_format_control: renderer %s\n", renderer ? renderer : "(none)");
    if (!renderer || !strstr(renderer, "Mali-G72")) return 3;
    int failed = draw(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, "RGBA8", 0);
    failed |= draw(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, "RGBA32UI", 1);
    failed |= draw(GL_RGBA32F, GL_RGBA, GL_FLOAT, "RGBA32F", 0);
    OSMesaDestroyContext(context);
    return failed;
}
