// ============================================================================
//  render_glsl.c -- portable, zero-third-party-dependency offline GLSL renderer
//
//  Renders a fragment shader (default: shaders/wave.glsl) frame by frame with a
//  virtual clock, reads each frame back, and pipes raw RGB into ffmpeg.
//
//  ONE file, THREE platform backends. Only the ~60 lines in the "platform
//  layer" block differ; every line of GL code below it is shared, so the three
//  ports cannot drift apart.
//
//    platform   context API   linker flags
//    ---------  -------------  ----------------------------------------------
//    macOS      CGL            -framework OpenGL
//    Windows    WGL            opengl32.lib gdi32.lib user32.lib
//    Linux      EGL            -lEGL -lGL        (headless, no X server needed)
//
//  Build
//    macOS    clang -O2 -o render_glsl render_glsl.c -framework OpenGL
//    Linux    cc    -O2 -o render_glsl render_glsl.c -lEGL -lGL
//    Windows  cl    /O2 render_glsl.c opengl32.lib gdi32.lib user32.lib
//             mingw: gcc -O2 -o render_glsl.exe render_glsl.c -lopengl32 -lgdi32 -luser32
//
//  Usage
//    ./render_glsl                          # 5 s of 1920x1080@60 into ./wave.mp4
//    ./render_glsl out/sine.mp4 3840 2160 120 10
//    SHADER=shaders/my.glsl ./render_glsl my.mp4 1280 720 30 2
//    NO_ENCODE=1 ./render_glsl x.mp4        # pipe to /dev/null: isolates encoder cost
//    FFMPEG=/opt/homebrew/bin/ffmpeg ./render_glsl       # ffmpeg binary (not codec)
//    ENCODER=h264_videotoolbox ENCODE_ARGS="-q:v 60" ./render_glsl   # hardware encode
//
//  Requires ffmpeg on PATH (that is the only external dependency).
// ============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#if defined(_WIN32)
  #include <direct.h>
#else
  #include <sys/stat.h>
#endif

/* ==========================================================================
 *  1. PLATFORM LAYER -- the only part that is not portable
 * ========================================================================== */

#ifdef __APPLE__

  #define GL_SILENCE_DEPRECATION 1
  #include <OpenGL/OpenGL.h>
  #include <OpenGL/gl3.h>
  #include <time.h>
  #define PLATFORM_NAME "macOS / CGL"

  typedef CGLContextObj plat_ctx_t;

  // macOS has no EGL/WGL, so the platform layer is CGL. A context without a
  // drawable is legal here; we render into an FBO.
  static int plat_init(plat_ctx_t *out) {
      CGLPixelFormatAttribute attrs[] = {
          kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
          kCGLPFAAccelerated,
          kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
          (CGLPixelFormatAttribute)0
      };
      CGLPixelFormatObj pix;
      GLint npix = 0;
      if (CGLChoosePixelFormat(attrs, &pix, &npix) != kCGLNoError || !pix) {
          fprintf(stderr, "[gl] CGLChoosePixelFormat failed\n");
          return 0;
      }
      if (CGLCreateContext(pix, NULL, out) != kCGLNoError) {
          fprintf(stderr, "[gl] CGLCreateContext failed\n");
          return 0;
      }
      CGLSetCurrentContext(*out);
      return 1;
  }
  static void plat_shutdown(plat_ctx_t ctx) { CGLSetCurrentContext(NULL); CGLDestroyContext(ctx); }

#elif defined(_WIN32)

  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <GL/gl.h>          /* GL 1.1 prototypes+enums: ships with the SDK */
  #include <time.h>
  #define PLATFORM_NAME "Windows / WGL"

  /* --- the part GLAD/GLEW normally does for you ---------------------------
   * opengl32.dll only exports GL 1.1. Everything newer must be fetched at
   * runtime with wglGetProcAddress, so we declare the ~24 entry points we use
   * by hand. This is exactly the boilerplate a loader library exists to hide.
   * ---------------------------------------------------------------------- */
  typedef char GLchar;
  typedef ptrdiff_t GLsizeiptr;

  #define GL_VERTEX_SHADER          0x8B31
  #define GL_FRAGMENT_SHADER        0x8B30
  #define GL_COMPILE_STATUS         0x8B81
  #define GL_LINK_STATUS            0x8B82
  #define GL_INFO_LOG_LENGTH        0x8B84
  #define GL_FRAMEBUFFER            0x8D40
  #define GL_RENDERBUFFER           0x8D41
  #define GL_COLOR_ATTACHMENT0      0x8CE0
  #define GL_FRAMEBUFFER_COMPLETE   0x8CD5
  #define GL_RGB8                   0x8051

  #define GL_FUNCS(X)                                                                     \
    X(GLuint,   glCreateShader,          (GLenum))                                        \
    X(void,     glShaderSource,          (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void,     glCompileShader,         (GLuint))                                        \
    X(void,     glGetShaderiv,           (GLuint, GLenum, GLint *))                       \
    X(void,     glGetShaderInfoLog,      (GLuint, GLsizei, GLsizei *, GLchar *))          \
    X(GLuint,   glCreateProgram,         (void))                                          \
    X(void,     glAttachShader,          (GLuint, GLuint))                                \
    X(void,     glLinkProgram,           (GLuint))                                        \
    X(void,     glGetProgramiv,          (GLuint, GLenum, GLint *))                       \
    X(void,     glGetProgramInfoLog,     (GLuint, GLsizei, GLsizei *, GLchar *))          \
    X(void,     glUseProgram,            (GLuint))                                        \
    X(void,     glGenFramebuffers,       (GLsizei, GLuint *))                             \
    X(void,     glBindFramebuffer,       (GLenum, GLuint))                                \
    X(void,     glGenRenderbuffers,      (GLsizei, GLuint *))                             \
    X(void,     glBindRenderbuffer,      (GLenum, GLuint))                                \
    X(void,     glRenderbufferStorage,   (GLenum, GLenum, GLsizei, GLsizei))              \
    X(void,     glFramebufferRenderbuffer,(GLenum, GLenum, GLenum, GLuint))               \
    X(GLenum,   glCheckFramebufferStatus,(GLenum))                                        \
    X(void,     glGenVertexArrays,       (GLsizei, GLuint *))                             \
    X(void,     glBindVertexArray,       (GLuint))                                        \
    X(GLint,    glGetUniformLocation,    (GLuint, const GLchar *))                        \
    X(void,     glUniform1f,             (GLint, GLfloat))                                \
    X(void,     glUniform2f,             (GLint, GLfloat, GLfloat))                       \
    X(void,     glDeleteShader,          (GLuint))

  #define X(ret, name, args) typedef ret (APIENTRY *PFN_##name) args; static PFN_##name p_##name;
  GL_FUNCS(X)
  #undef X

  typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
  static PFN_wglCreateContextAttribsARB p_wglCreateContextAttribsARB;
  #define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
  #define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
  #define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
  #define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

  typedef struct { HDC dc; HWND hwnd; HGLRC rc; HINSTANCE inst; } plat_ctx_t;

  static LRESULT CALLBACK wave_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
      return DefWindowProcA(h, m, w, l);
  }

  // Windows needs the classic two-step dance: a throwaway legacy context first,
  // only then can wglCreateContextAttribsARB be resolved to make a 4.1 core one.
  static int plat_init(plat_ctx_t *out) {
      out->inst = GetModuleHandleA(NULL);
      WNDCLASSA wc;
      memset(&wc, 0, sizeof wc);
      wc.lpfnWndProc = wave_wndproc;
      wc.hInstance = out->inst;
      wc.lpszClassName = "glslwave";
      if (!RegisterClassA(&wc)) { fprintf(stderr, "[gl] RegisterClass failed\n"); return 0; }

      out->hwnd = CreateWindowA("glslwave", "", WS_POPUP, 0, 0, 1, 1, NULL, NULL, out->inst, NULL);
      if (!out->hwnd) { fprintf(stderr, "[gl] CreateWindow failed\n"); return 0; }
      out->dc = GetDC(out->hwnd);

      PIXELFORMATDESCRIPTOR pfd;
      memset(&pfd, 0, sizeof pfd);
      pfd.nSize = sizeof pfd;
      pfd.nVersion = 1;
      pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
      pfd.iPixelType = PFD_TYPE_RGBA;
      pfd.cColorBits = 24;
      pfd.cDepthBits = 24;
      int pf = ChoosePixelFormat(out->dc, &pfd);
      if (!pf) { fprintf(stderr, "[gl] ChoosePixelFormat failed\n"); return 0; }
      SetPixelFormat(out->dc, pf, &pfd);

      HGLRC legacy = wglCreateContext(out->dc);
      if (!legacy) { fprintf(stderr, "[gl] wglCreateContext failed\n"); return 0; }
      wglMakeCurrent(out->dc, legacy);

      p_wglCreateContextAttribsARB =
          (PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");
      if (!p_wglCreateContextAttribsARB) {
          fprintf(stderr, "[gl] WGL_ARB_create_context missing -- need a GL 3.2+ driver\n");
          return 0;
      }
      int attribs[] = {
          WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
          WGL_CONTEXT_MINOR_VERSION_ARB, 1,
          WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
          0
      };
      out->rc = p_wglCreateContextAttribsARB(out->dc, NULL, attribs);
      wglMakeCurrent(NULL, NULL);
      wglDeleteContext(legacy);
      if (!out->rc) { fprintf(stderr, "[gl] core-profile context creation failed\n"); return 0; }
      wglMakeCurrent(out->dc, out->rc);

      #define X(ret, name, args) p_##name = (PFN_##name)wglGetProcAddress(#name);
      GL_FUNCS(X)
      #undef X
      return 1;
  }
  static void plat_shutdown(plat_ctx_t ctx) {
      wglMakeCurrent(NULL, NULL);
      wglDeleteContext(ctx.rc);
      ReleaseDC(ctx.hwnd, ctx.dc);
      DestroyWindow(ctx.hwnd);
  }

  /* after plat_init(), the short names must resolve to the loaded pointers */
  #define glCreateShader          p_glCreateShader
  #define glShaderSource          p_glShaderSource
  #define glCompileShader         p_glCompileShader
  #define glGetShaderiv           p_glGetShaderiv
  #define glGetShaderInfoLog      p_glGetShaderInfoLog
  #define glCreateProgram         p_glCreateProgram
  #define glAttachShader          p_glAttachShader
  #define glLinkProgram           p_glLinkProgram
  #define glGetProgramiv          p_glGetProgramiv
  #define glGetProgramInfoLog     p_glGetProgramInfoLog
  #define glUseProgram            p_glUseProgram
  #define glGenFramebuffers       p_glGenFramebuffers
  #define glBindFramebuffer       p_glBindFramebuffer
  #define glGenRenderbuffers      p_glGenRenderbuffers
  #define glBindRenderbuffer      p_glBindRenderbuffer
  #define glRenderbufferStorage   p_glRenderbufferStorage
  #define glFramebufferRenderbuffer p_glFramebufferRenderbuffer
  #define glCheckFramebufferStatus p_glCheckFramebufferStatus
  #define glGenVertexArrays       p_glGenVertexArrays
  #define glBindVertexArray       p_glBindVertexArray
  #define glGetUniformLocation    p_glGetUniformLocation
  #define glUniform1f             p_glUniform1f
  #define glUniform2f             p_glUniform2f

#elif defined(__linux__)

  #define GL_GLEXT_PROTOTYPES 1
  #include <GL/gl.h>
  #include <GL/glext.h>
  #include <EGL/egl.h>
  #include <time.h>
  #define PLATFORM_NAME "Linux / EGL (headless)"

  typedef struct { EGLDisplay dpy; EGLContext ctx; EGLSurface surf; } plat_ctx_t;

#ifndef EGL_PLATFORM_SURFACELESS_MESA
  #define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
  typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYEXTPROC)(EGLenum, void *, const EGLint *);

  /* Pick a config, optionally requiring pbuffer support. Mesa's surfaceless
     platform may offer no pbuffer configs at all, hence the second attempt. */
  static int pick_config(EGLDisplay d, int want_pbuffer, EGLConfig *out) {
      EGLint a[16];
      int i = 0;
      a[i++] = EGL_RENDERABLE_TYPE; a[i++] = EGL_OPENGL_BIT;
      if (want_pbuffer) { a[i++] = EGL_SURFACE_TYPE; a[i++] = EGL_PBUFFER_BIT; }
      a[i++] = EGL_RED_SIZE;   a[i++] = 8;
      a[i++] = EGL_GREEN_SIZE; a[i++] = 8;
      a[i++] = EGL_BLUE_SIZE;  a[i++] = 8;
      a[i++] = EGL_NONE;
      EGLint n = 0;
      if (!eglChooseConfig(d, a, out, 1, &n) || n < 1) return 0;
      return 1;
  }

  // No X server required: EGL runs fully headless. We prefer the surfaceless
  // platform (servers, CI), then fall back to a default display, and finally to
  // a 1x1 pbuffer if the context cannot be made current without a surface.
  static int plat_init(plat_ctx_t *out) {
      out->dpy = EGL_NO_DISPLAY; out->ctx = EGL_NO_CONTEXT; out->surf = EGL_NO_SURFACE;

      PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
          (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
      if (get_platform_display)
          out->dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
      if (out->dpy == EGL_NO_DISPLAY)
          out->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
      if (out->dpy == EGL_NO_DISPLAY) {
          fprintf(stderr, "[gl] eglGetDisplay failed (0x%x)\n", eglGetError());
          return 0;
      }
      EGLint maj, min;
      if (!eglInitialize(out->dpy, &maj, &min)) {
          fprintf(stderr, "[gl] eglInitialize failed (0x%x)\n", eglGetError());
          return 0;
      }
      if (!eglBindAPI(EGL_OPENGL_API)) {
          fprintf(stderr, "[gl] eglBindAPI(EGL_OPENGL_API) failed (0x%x)\n", eglGetError());
          return 0;
      }

      EGLConfig cfg;
      if (!pick_config(out->dpy, 1, &cfg) && !pick_config(out->dpy, 0, &cfg)) {
          fprintf(stderr, "[gl] eglChooseConfig found no config (0x%x)\n", eglGetError());
          return 0;
      }

      EGLint ctx_attrs[] = {
          EGL_CONTEXT_MAJOR_VERSION, 4,
          EGL_CONTEXT_MINOR_VERSION, 1,
          EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
          EGL_NONE
      };
      out->ctx = eglCreateContext(out->dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
      if (out->ctx == EGL_NO_CONTEXT) {
          fprintf(stderr, "[gl] eglCreateContext(GL 4.1 core) failed (0x%x).\n"
                          "     Mesa too old? lower the version here and in the shader header.\n",
                  eglGetError());
          return 0;
      }

      if (!eglMakeCurrent(out->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, out->ctx)) {
          EGLint pb[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
          out->surf = eglCreatePbufferSurface(out->dpy, cfg, pb);
          if (out->surf == EGL_NO_SURFACE ||
              !eglMakeCurrent(out->dpy, out->surf, out->surf, out->ctx)) {
              fprintf(stderr, "[gl] eglMakeCurrent failed (0x%x)\n", eglGetError());
              return 0;
          }
      }
      return 1;
  }
  static void plat_shutdown(plat_ctx_t ctx) {
      eglMakeCurrent(ctx.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (ctx.surf != EGL_NO_SURFACE) eglDestroySurface(ctx.dpy, ctx.surf);
      eglDestroyContext(ctx.dpy, ctx.ctx);
      eglTerminate(ctx.dpy);
  }

#else
  #error "unsupported platform: add a CGL / WGL / EGL block above"
#endif

/* ==========================================================================
 *  2. PORTABLE HELPERS
 * ========================================================================== */

#if defined(_WIN32)
  #include <windows.h>
  static double now_s(void) {
      static LARGE_INTEGER freq;
      static int init = 0;
      LARGE_INTEGER c;
      if (!init) { QueryPerformanceFrequency(&freq); init = 1; }
      QueryPerformanceCounter(&c);
      return (double)c.QuadPart / (double)freq.QuadPart;
  }
  #define POPEN(cmd)  _popen((cmd), "wb")     /* "b" matters on Windows */
  #define PCLOSE(p)   _pclose(p)
#else
  #include <time.h>
  static double now_s(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return ts.tv_sec + ts.tv_nsec * 1e-9;
  }
  #define POPEN(cmd)  popen((cmd), "w")
  #define PCLOSE(p)   pclose(p)
#endif

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[gl] cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "[gl] read error\n"); exit(1); }
    buf[n] = 0;
    fclose(f);
    return buf;
}

static int file_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* So `./build/render_glsl` works from the repo root or from a build dir. */
static const char *find_shader(const char *from_env) {
    static const char *cands[] = { NULL, "shaders/wave.glsl",
                                   "../shaders/wave.glsl", "wave.glsl" };
    if (from_env) return from_env;
    for (unsigned i = 1; i < sizeof cands / sizeof cands[0]; i++)
        if (file_exists(cands[i])) return cands[i];
    return "shaders/wave.glsl";          /* miss -> read_file explains it */
}

/* ffmpeg does not create the output directory, and a first-run failure there
   is annoying, so create one level if needed. */
static void ensure_parent_dir(const char *path) {
    const char *slash = strrchr(path, '/');
#if defined(_WIN32)
    const char *bs = strrchr(path, '\\');
    if (bs && (!slash || bs > slash)) slash = bs;
#endif
    if (!slash || slash == path) return;
    char dir[1024];
    size_t n = (size_t)(slash - path);
    if (n == 0 || n >= sizeof dir) return;
    memcpy(dir, path, n);
    dir[n] = 0;
#if defined(_WIN32)
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

/* ==========================================================================
 *  3. THE RENDERER -- identical on all three platforms
 * ========================================================================== */

static const char *VERT_SRC =
    "#version 410 core\n"
    "void main() {\n"
    "  vec2 v[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));\n"
    "  gl_Position = vec4(v[gl_VertexID], 0.0, 1.0);\n"
    "}\n";

static const char *FRAG_HEADER =
    "#version 410 core\n"
    "uniform vec2  uResolution;\n"
    "uniform float uTime;\n"
    "out vec4 fragColor;\n";

static GLuint compile(GLenum type, const char *src, const char *label) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "[gl] %s shader failed:\n%s\n", label, log);
        exit(1);
    }
    return s;
}

int main(int argc, char **argv) {
    const char *out_path = argc > 1 ? argv[1] : "wave.mp4";
    int    W   = argc > 2 ? atoi(argv[2]) : 1920;
    int    H   = argc > 3 ? atoi(argv[3]) : 1080;
    int    fps = argc > 4 ? atoi(argv[4]) : 60;
    double dur = argc > 5 ? atof(argv[5]) : 5.0;
    const char *shader_path = find_shader(getenv("SHADER"));

    ensure_parent_dir(out_path);

    plat_ctx_t pctx;
    if (!plat_init(&pctx)) return 1;

    printf("[gl] %s\n", PLATFORM_NAME);
    printf("[gl] %s | %s\n", (const char *)glGetString(GL_RENDERER),
                            (const char *)glGetString(GL_VERSION));

    /* --- compile the shader: shared body + a platform-agnostic header ------ */
    char *body = read_file(shader_path);
    size_t len = strlen(FRAG_HEADER) + strlen(body) + 1;
    char *frag_src = malloc(len);
    strcpy(frag_src, FRAG_HEADER);
    strcat(frag_src, body);

    GLuint vs = compile(GL_VERTEX_SHADER, VERT_SRC, "vertex");
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag_src, "fragment");
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[8192];
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        fprintf(stderr, "[gl] link failed:\n%s\n", log);
        return 1;
    }
    glUseProgram(prog);

    /* --- offscreen target: no window is ever shown ------------------------- */
    GLuint fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[gl] framebuffer incomplete\n");
        return 1;
    }
    glViewport(0, 0, W, H);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);   /* RGB rows are 3*W bytes, may be odd */

    /* core profile requires a bound VAO even with no vertex attributes */
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glUniform2f(glGetUniformLocation(prog, "uResolution"), (GLfloat)W, (GLfloat)H);
    GLint uTime = glGetUniformLocation(prog, "uTime");

    /* --- ffmpeg sink ------------------------------------------------------- */
    int total = (int)(dur * fps + 0.5);
    char cmd[2048];
    if (getenv("NO_ENCODE")) {
        /* diagnostics: whatever disappears from the pipe timing was encoder
           backpressure, not our write cost */
        #if defined(_WIN32)
            snprintf(cmd, sizeof cmd, "more > NUL");
        #else
            snprintf(cmd, sizeof cmd, "cat > /dev/null");
        #endif
    } else {
        const char *enc   = getenv("FFMPEG")  ? getenv("FFMPEG")  : "ffmpeg";
        const char *codec = getenv("ENCODER") ? getenv("ENCODER") : "libx264";
        /* CRF is an x264/x265 thing; hardware encoders want -q:v instead, so
           the quality flags are overridable too. */
        const char *qual  = getenv("ENCODE_ARGS") ? getenv("ENCODE_ARGS") : "-crf 16 -preset slow";
        snprintf(cmd, sizeof cmd,
                 "%s -hide_banner -loglevel error -y "
                 "-f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
                 "-vf vflip -c:v %s %s "
                 "-pix_fmt yuv420p -movflags +faststart \"%s\"",
                 enc, W, H, fps, codec, qual, out_path);
    }
    FILE *pipe = POPEN(cmd);
    if (!pipe) { fprintf(stderr, "[gl] cannot start encoder: %s\n", cmd); return 1; }

    unsigned char *px = malloc((size_t)W * H * 3);
    double t_shade = 0, t_read = 0, t_pipe = 0;
    double wall0 = now_s();

    for (int i = 0; i < total; i++) {
        double t = (double)i / fps;          /* virtual clock: frame-exact output */

        double a = now_s();
        glUniform1f(uTime, (GLfloat)t);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        double b = now_s();
        glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px);
        double c = now_s();
        fwrite(px, 1, (size_t)W * H * 3, pipe);
        double d = now_s();

        t_shade += b - a; t_read += c - b; t_pipe += d - c;
        if ((i + 1) % 60 == 0 || i + 1 == total)
            fprintf(stderr, "\r[gl] frame %d/%d", i + 1, total);
    }
    PCLOSE(pipe);
    fprintf(stderr, "\n");

    double wall = now_s() - wall0;
    printf("[gl] done in %.2fs  (%.2f ms/frame, %.1f fps incl. encode)\n",
           wall, wall / total * 1000.0, total / wall);
    printf("[gl]   shader+GPU wait : %6.2f ms/frame\n", t_shade / total * 1000.0);
    printf("[gl]   glReadPixels    : %6.2f ms/frame\n", t_read / total * 1000.0);
    printf("[gl]   pipe to encoder : %6.2f ms/frame\n", t_pipe / total * 1000.0);
    if (!getenv("NO_ENCODE")) printf("[gl] written: %s\n", out_path);

    free(px);
    plat_shutdown(pctx);
    return 0;
}
