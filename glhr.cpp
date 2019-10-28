// Hyperbolic Rogue -- low-level OpenGL routines
// Copyright (C) 2011-2019 Zeno Rogue, see 'hyper.cpp' for details

/** \file glhr.cpp
 *  \brief low-level OpenGL routines
 *
 *  If CAP_SHADER is 0, OpenGL 1.0 is used.
 *  If CAP_SHADER is 1, we are using GLSL shaders.
 */

#include "hyper.h"
namespace hr {

#ifndef DEBUG_GL
#define DEBUG_GL 0
#endif

// Copyright (C) 2011-2018 Zeno Rogue, see 'hyper.cpp' for details

EX void glError(const char* GLcall, const char* file, const int line) {
  GLenum errCode = glGetError();
  if(errCode!=GL_NO_ERROR) {
    println(hlog, format("OPENGL ERROR #%i: in file %s on line %i :: %s\n",errCode,file, line, GLcall));
    }
  }

#ifndef CAP_VERTEXBUFFER
#define CAP_VERTEXBUFFER (ISWEB)
#endif

#if HDR
#if CAP_SHADER && CAP_NOSHADER
#define WITHSHADER(x, y) if(glhr::noshaders) y else x
#else
#if CAP_NOSHADER
#define WITHSHADER(x, y) if(1) y
#else
#define WITHSHADER(x, y) if(1) x
#endif
#endif
#endif

EX namespace glhr {

#if HDR
struct glmatrix {
  GLfloat a[4][4];
  GLfloat* operator[] (int i) { return a[i]; }
  const GLfloat* operator[] (int i) const { return a[i]; }
  GLfloat* as_array() { return a[0]; }
  const GLfloat* as_array() const { return a[0]; }
  array<float, 16>& as_stdarray() { return *(array<float, 16>*)this; }
  const array<float, 16>& as_stdarray() const { return *(array<float, 16>*)this; }
  };

glvertex pointtogl(const hyperpoint& t);

  inline glvertex makevertex(GLfloat x, GLfloat y, GLfloat z) {
    #if SHDIM == 3
    return make_array(x, y, z);
    #else
    return make_array<GLfloat>(x, y, z, 1);
    #endif
    }
  
  struct colored_vertex {
    glvertex coords;
    glvec4 color;
    colored_vertex(GLfloat x, GLfloat y, GLfloat r, GLfloat g, GLfloat b) {
      coords[0] = x;
      coords[1] = y;
      coords[2] = 0;
      coords[3] = 1;
      color[0] = r;
      color[1] = g;
      color[2] = b;
      color[3] = 1;
      }
    colored_vertex(hyperpoint h, color_t col) {
      coords = pointtogl(h);
      for(int i=0; i<4; i++)
        color[i] = part(col, 3-i) / 255.0;
      }
    };
  
  struct textured_vertex {
    glvertex coords;
    glvec2 texture;
    };
  
  struct ct_vertex {
    glvertex coords;
    glvec4 color;
    glvec2 texture;
    ct_vertex(const hyperpoint& h, ld x1, ld y1, ld col) {
      coords = pointtogl(h);
      texture[0] = x1;
      texture[1] = y1;
      color[0] = color[1] = color[2] = col;
      color[3] = 1;
      }
    };  

#endif

#if CAP_SHADER
EX bool noshaders = false;
#else
EX bool noshaders = true;
#endif

bool glew   = false;

bool current_depthtest, current_depthwrite;
ld fogbase;

#if HDR
typedef const void *constvoidptr;
#endif

EX constvoidptr current_vertices, buffered_vertices;
ld current_linewidth;

GLuint buf_current, buf_buffered;

void display(const glmatrix& m) {
  for(int i=0; i<4; i++) {
    for(int j=0; j<4; j++)
      printf("%10.5f", m[i][j]);
    printf("\n");
    }
  printf("\n");
  }

glmatrix operator * (glmatrix m1, glmatrix m2) {
  glmatrix res;
  for(int i=0; i<4; i++)
  for(int j=0; j<4; j++) {
    res[i][j] = 0;
    for(int k=0; k<4; k++)
      res[i][j] += m1[i][k] * m2[k][j];
    }
  return res;
  }

EX glmatrix id = {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}}};

EX glmatrix scale(ld x, ld y, ld z) {
  glmatrix tmp;
  for(int i=0; i<4; i++)
  for(int j=0; j<4; j++)
    tmp[i][j] = (i==j);
  tmp[0][0] = x;
  tmp[1][1] = y;
  tmp[2][2] = z;
  return tmp;
  }

EX glmatrix tmtogl(const transmatrix& T) {
  glmatrix tmp;
  for(int i=0; i<4; i++)
  for(int j=0; j<4; j++)
    tmp[i][j] = T[i][j];
  return tmp;
  }

EX glmatrix tmtogl_transpose(const transmatrix& T) {
  glmatrix tmp;
  for(int i=0; i<4; i++)
  for(int j=0; j<4; j++)
    tmp[i][j] = T[j][i];
  return tmp;
  }

EX glmatrix tmtogl_transpose3(const transmatrix& T) {
  glmatrix tmp;
  for(int i=0; i<4; i++)
  for(int j=0; j<4; j++)
    tmp[i][j] = T[j][i];
  if(MDIM == 3)
    for(int i=0; i<4; i++)
    for(int j=0; j<4; j++)
      if(i == 3 || j == 3) tmp[j][i] = 0;
  return tmp;
  }

EX glmatrix ortho(ld x, ld y, ld z) {
  return scale(1/x, 1/y, 1/z);
  }

EX glmatrix& as_glmatrix(GLfloat o[16]) {
  glmatrix& tmp = (glmatrix&) (o[0]);
  return tmp;
  }

#if HDR
constexpr ld vnear_default = 1e-3;
constexpr ld vfar_default = 1e9;
#endif

EX glmatrix frustum(ld x, ld y, ld vnear IS(vnear_default), ld vfar IS(vfar_default)) {
  GLfloat frustum[16] = {
    GLfloat(1 / x), 0, 0, 0,
    0, GLfloat(1 / y), 0, 0,
    0, 0, GLfloat(-(vnear+vfar)/(vfar-vnear)), -1,
    0, 0, GLfloat(-2*vnear*vfar/(vfar-vnear)), 0};
  return as_glmatrix(frustum);
  }

EX glmatrix translate(ld x, ld y, ld z) {
  glmatrix tmp;
  for(int i=0; i<4; i++)
  for(int j=0; j<4; j++)
    tmp[i][j] = (i==j);
  tmp[3][0] = x;
  tmp[3][1] = y;
  tmp[3][2] = z;
  return tmp;
  }

// ** legacy **

// /* shaders */

glmatrix projection;

EX void new_projection() {
  WITHSHADER({
    projection = id;
    }, {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    return;
    })
  }

EX void projection_multiply(const glmatrix& m) {
  WITHSHADER({
    projection = m * projection;  
    }, {
    glMatrixMode(GL_PROJECTION);
    glMultMatrixf(m.as_array());
    })
  }

EX void init();

#if HDR
struct GLprogram {
  GLuint _program;
  GLuint vertShader, fragShader;

  GLint uFog, uFogColor, uColor, tTexture, tInvExpTable, uMV, uProjection, uAlpha, uFogBase, uPP;
  GLint uPRECX, uPRECY, uPRECZ, uIndexSL, uIterations;
  
  flagtype shader_flags;
  
  string _vsh, _fsh;
  
  GLprogram(string vsh, string fsh);

  ~GLprogram();
  };
#endif

EX shared_ptr<GLprogram> current_glprogram = nullptr;

EX bool debug_gl;

EX int compileShader(int type, const string& s) {
  GLint status;

  if(debug_gl) {
    println(hlog, "===\n");
    int lineno = 1;
    string cline = "";
    for(char c: s+"\n") {
      if(c == '\n') println(hlog, format("%4d : ", lineno), cline), lineno++, cline = "";
      else cline += c;
      }
    println(hlog, "===");
    }
  
  GLint shader = glCreateShader(type);
  const char *ss = s.c_str();
  glShaderSource(shader, 1, &ss, NULL);
  glCompileShader(shader);
  
  GLint logLength;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
  if (logLength > 0) {
    std::vector<char> log(logLength);
    glGetShaderInfoLog(shader, logLength, &logLength, log.data());
    if(logLength > 0)
      printf("compiler log (%d): '%s'\n", logLength, log.data());
    if(debug_gl) exit(1);
    }
  
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == 0) {
    glDeleteShader(shader);
    printf("failed to compile shader\n");
    println(hlog, s);
    shader = 0;
    }
  
  return shader;
  }

GLprogram::GLprogram(string vsh, string fsh) {
  if(noshaders) {
    uFog = -1;
    uProjection = -1;
    uPP = -1;
    uMV = -1;
    uPRECX = -1;
    uIterations = -1;
    uAlpha = -1;
    return;
    }
  
  _vsh = vsh; _fsh = fsh;
  _program = glCreateProgram();
  
  add_fixed_functions(vsh);
  add_fixed_functions(fsh);
  
  // printf("creating program %d\n", _program);
  vertShader = compileShader(GL_VERTEX_SHADER, vsh.c_str());
  fragShader = compileShader(GL_FRAGMENT_SHADER, fsh.c_str());

  // Attach vertex shader to program.
  glAttachShader(_program, vertShader);
  
  // Attach fragment shader to program.
  glAttachShader(_program, fragShader);
  
  glBindAttribLocation(_program, aPosition, "aPosition");
  glBindAttribLocation(_program, aTexture, "aTexture");
  glBindAttribLocation(_program, aColor, "aColor");

  GLint status;
  glLinkProgram(_program);
    
  GLint logLength;
  glGetProgramiv(_program, GL_INFO_LOG_LENGTH, &logLength);
  if (logLength > 0) {
    std::vector<char> log(logLength);
    glGetProgramInfoLog(_program, logLength, &logLength, log.data());
    if(logLength > 0)
      printf("linking log (%d): %s\n", logLength, log.data());
    }
   
  glGetProgramiv(_program, GL_LINK_STATUS, &status);
  if (status == 0) {
    printf("failed to link shader\n");
    exit(1);
    }
  
  uMV = glGetUniformLocation(_program, "uMV");
  uProjection = glGetUniformLocation(_program, "uP");
  uPP = glGetUniformLocation(_program, "uPP");
  uFog = glGetUniformLocation(_program, "uFog");
  uFogColor = glGetUniformLocation(_program, "uFogColor");
  uFogBase = glGetUniformLocation(_program, "uFogBase");
  uAlpha = glGetUniformLocation(_program, "uAlpha");
  uColor = glGetUniformLocation(_program, "uColor");
  tTexture = glGetUniformLocation(_program, "tTexture");
  tInvExpTable = glGetUniformLocation(_program, "tInvExpTable");

  uPRECX = glGetUniformLocation(_program, "PRECX");
  uPRECY = glGetUniformLocation(_program, "PRECY");
  uPRECZ = glGetUniformLocation(_program, "PRECZ");
  uIndexSL = glGetUniformLocation(_program, "uIndexSL");
  uIterations = glGetUniformLocation(_program, "uIterations");  
  }

GLprogram::~GLprogram() {
  glDeleteProgram(_program);
  if(vertShader) glDeleteShader(vertShader), vertShader = 0;
  if(fragShader) glDeleteShader(fragShader), fragShader = 0;
  }

EX void set_index_sl(ld x) {
  glUniform1f(glhr::current_glprogram->uIndexSL, x);
  }

EX void set_sl_iterations(int steps) {
  glUniform1i(glhr::current_glprogram->uIterations, steps);
  }

EX void set_solv_prec(int x, int y, int z) {
  glUniform1i(glhr::current_glprogram->tInvExpTable, INVERSE_EXP_BINDING);
  glUniform1f(glhr::current_glprogram->uPRECX, x);
  glUniform1f(glhr::current_glprogram->uPRECY, y);
  glUniform1f(glhr::current_glprogram->uPRECZ, z);
  }

EX glmatrix current_matrix, current_modelview, current_projection;

bool operator == (const glmatrix& m1, const glmatrix& m2) {
  for(int i=0; i<4; i++) 
    for(int j=0; j<4; j++)
      if(m1[i][j] != m2[i][j]) return false;
  return true;
  }

bool operator != (const glmatrix& m1, const glmatrix& m2) {
  return !(m1 == m2);
  }

EX glmatrix eyeshift;
EX bool using_eyeshift;

EX void set_modelview(const glmatrix& modelview) {
  #if CAP_NOSHADER
  if(noshaders) {
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(modelview.as_array());
    return;
    }
  #endif
  auto& cur = current_glprogram;
  if(!cur) return;

  if(using_eyeshift) {
    glmatrix mvp = modelview * eyeshift;
    #if MINIMIZE_GL_CALLS
    if(mvp == current_matrix) return;
    current_matrix = mvp;
    #endif
    glUniformMatrix4fv(cur->uMV, 1, 0, mvp.as_array());
    }
  else if(modelview != current_modelview) {
    current_modelview = modelview;
    glUniformMatrix4fv(cur->uMV, 1, 0, modelview.as_array());
    }
  if(projection != current_projection) {
    current_projection = projection;
    glUniformMatrix4fv(cur->uProjection, 1, 0, projection.as_array());
    }
  }

EX void id_modelview() {
  #if CAP_NOSHADER
  if(noshaders) {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    return;
    }
  #endif
  set_modelview(id);
  }

EX array<GLfloat, 4> acolor(color_t color, ld scale IS(1)) {
  array<GLfloat, 4> cols;
  for(int i=0; i<4; i++)
    cols[i] = part(color, 3-i) / 255.0 * scale;
  return cols;
  }


EX void color2(color_t color, ld scale IS(1)) {
  auto cols = acolor(color, scale);
    
  WITHSHADER({
    if(!current_glprogram) return;
    glUniform4f(current_glprogram->uColor, cols[0], cols[1], cols[2], cols[3]);
    }, {
    glColor4f(cols[0], cols[1], cols[2], cols[3]);
    }
    )
  }

EX void colorClear(color_t color) {
  glClearColor(part(color, 3) / 255.0, part(color, 2) / 255.0, part(color, 1) / 255.0, part(color, 0) / 255.0);
}

EX void full_enable(shared_ptr<GLprogram> p) {
  auto& cur = current_glprogram;
  flagtype oldflags = cur ? cur->shader_flags : 0;
  if(p == cur) return;
  cur = p;
  GLERR("pre_switch_mode");
  WITHSHADER({
    glUseProgram(cur->_program);
    GLERR("after_enable");
    }, {
    });
  reset_projection();
  flagtype newflags = cur->shader_flags;
  tie(oldflags, newflags) = make_pair(oldflags & ~newflags, newflags & ~oldflags);

  if(newflags & GF_TEXTURE) {
    GLERR("xsm");
    WITHSHADER({
      glEnableVertexAttribArray(aTexture);
      GLERR("xsm");
      }, {
      glEnable(GL_TEXTURE_2D);
      glEnableClientState(GL_TEXTURE_COORD_ARRAY);
      GLERR("xsm");
      })
    }
  if(oldflags & GF_TEXTURE) {
    GLERR("xsm");
    WITHSHADER({
      glDisableVertexAttribArray(aTexture);
      GLERR("xsm");
      }, {
      glDisableClientState(GL_TEXTURE_COORD_ARRAY);
      glDisable(GL_TEXTURE_2D);
      GLERR("xsm");
      })
    }
  if(newflags & GF_VARCOLOR) {
    WITHSHADER({
      GLERR("xsm");
      glEnableVertexAttribArray(aColor);
      }, {
      GLERR("xsm");
      glEnableClientState(GL_COLOR_ARRAY);
      GLERR("xsm");
      })
    }
  if(oldflags & GF_VARCOLOR) {
    WITHSHADER({
      glDisableVertexAttribArray(aColor);
      GLERR("xsm");
      }, {
      glDisableClientState(GL_COLOR_ARRAY);
      GLERR("xsm");
      })
    }
  if(newflags & GF_LIGHTFOG) {
    #ifdef GLES_ONLY
    #define glFogi glFogx
    #endif
    WITHSHADER({}, {
    /*GLfloat light_ambient[] = { 3.5, 3.5, 3.5, 1.0 };
    GLfloat light_diffuse[] = { 1.0, 1.0, 1.0, 1.0 };
    GLfloat light_position[] = { 0.0, 0.0, 0.0, 1.0 };
  
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
  
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    GLERR("lighting");
  
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0); */
 
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 0);
    })
    }
  if(oldflags & GF_LIGHTFOG) {
    WITHSHADER({}, {glDisable(GL_FOG);})
    }
  WITHSHADER({
    glUniform1f(cur->uFogBase, 1); fogbase = 1;
    }, {})
  GLERR("after_switch_mode");
  current_vertices = NULL;
  WITHSHADER({
    current_matrix[0][0] = -1e8; // invalid
    current_modelview[0][0] = -1e8;
    current_projection[0][0] = -1e8;
    }, {})
  id_modelview();
  current_linewidth = -1;
  /* if(current_depthwrite) glDepthMask(GL_TRUE);
  else glDepthMask(GL_FALSE);
  if(current_depthtest) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST); */
  }

EX void fog_max(ld fogmax, color_t fogcolor) {
  WITHSHADER({
    glUniform1f(current_glprogram->uFog, 1 / fogmax);
  
    GLfloat cols[4];
    for(int i=0; i<4; i++) cols[i] = part(fogcolor, 3-i) / 255.0;
    glUniform4f(current_glprogram->uFogColor, cols[0], cols[1], cols[2], cols[3]);
    }, {
    glFogf(GL_FOG_END, fogmax);
    })
  }

EX void set_fogbase(ld _fogbase) {
  WITHSHADER({
    if(fogbase != _fogbase) {
      fogbase = _fogbase;
      glUniform1f(current_glprogram->uFogBase, fogbase);
      }
    }, {})
  }

EX void set_ualpha(ld alpha) {
  WITHSHADER({
    glUniform1f(current_glprogram->uAlpha, alpha);
    }, {})
  }

void init() {

  #if CAP_GLEW
    if(!glew) { 
      glew = true; 
      printf("Initializing GLEW\n");
      GLenum err = glewInit();
      if (GLEW_OK != err) {
        addMessage("Failed to initialize GLEW");
        printf("Failed to initialize GLEW\n");
        return;
        }
      printf("CreateProgram = %p\n", __glewCreateProgram);
      if(!__glewCreateProgram) noshaders = true;
      }
  #endif
  
  projection = id;

  WITHSHADER(glEnableVertexAttribArray(aPosition);, glEnableClientState(GL_VERTEX_ARRAY);)
  GLERR("aPosition");
  // #endif

  #if CAP_VERTEXBUFFER
  glGenBuffers(1, &buf_current);
  glGenBuffers(1, &buf_buffered);
  current_vertices = NULL;
  buffered_vertices = (void*) &buffered_vertices; // point to nothing
  glBindBuffer(GL_ARRAY_BUFFER, buf_current);
  #endif
  }

EX hyperpoint gltopoint(const glvertex& t) {
  hyperpoint h;
  h[0] = t[0]; h[1] = t[1]; h[2] = t[2]; 
  if(SHDIM == 4 && MAXMDIM == 4) h[3] = t[3];
  return h;
  }

EX glvertex pointtogl(const hyperpoint& t) {
  glvertex h;
  h[0] = t[0]; h[1] = t[1]; h[2] = t[2]; 
  if(SHDIM == 4) h[3] = (MDIM == 4) ? t[3] : 1;
  return h;
  }

#if CAP_VERTEXBUFFER
template<class T> void bindbuffer(T& v) {
  if(current_vertices == buffered_vertices || current_vertices == nullptr) {
    glBindBuffer(GL_ARRAY_BUFFER, buf_current);
    }
  current_vertices = &v[0];
  glBufferData(GL_ARRAY_BUFFER, isize(v) * sizeof(v[0]), &v[0], GL_DYNAMIC_DRAW);    
  }

#define PTR(attrib, q, field) \
  glVertexAttribPointer(attrib, q, GL_FLOAT, GL_FALSE, sizeof(v[0]), (void*) ((char*) &v[0].field - (char*) &v[0]));

#endif

EX void vertices(const vector<glvertex>& v, int vshift IS(0)) {
  #if CAP_VERTEXBUFFER
  if(&v[0] == buffered_vertices) {
    if(&v[0] == current_vertices) return;
    current_vertices = buffered_vertices;
    glBindBuffer(GL_ARRAY_BUFFER, buf_buffered);
    glVertexAttribPointer(glhr::aPosition, SHDIM, GL_FLOAT, GL_FALSE, sizeof(glvertex), 0);
    return;
    }
  bindbuffer(v);
  glVertexAttribPointer(glhr::aPosition, SHDIM, GL_FLOAT, GL_FALSE, sizeof(glvertex), 0);
  #else
  if(current_vertices == &v[vshift]) return;
  current_vertices = &v[vshift];
  WITHSHADER(
    glVertexAttribPointer(aPosition, SHDIM, GL_FLOAT, GL_FALSE, sizeof(glvertex), &v[vshift]);,
    glVertexPointer(SHDIM, GL_FLOAT, sizeof(glvertex), &v[0]);
    )
  #endif
  }

EX void vertices_texture(const vector<glvertex>& v, const vector<glvertex>& t, int vshift IS(0), int tshift IS(0)) {
  #if CAP_VERTEXBUFFER
  int q = min(isize(v)-vshift, isize(t)-tshift);
  vector<textured_vertex> tv(q);
  for(int i=0; i<q; i++)
    tv[i].coords = v[vshift+i],
    tv[i].texture[0] = t[tshift+i][0],
    tv[i].texture[1] = t[tshift+i][1];
  prepare(tv);
  #else
  vertices(v, vshift);
  WITHSHADER(
    glVertexAttribPointer(aTexture, SHDIM, GL_FLOAT, GL_FALSE, sizeof(glvertex), &t[tshift]);,
    glTexCoordPointer(SHDIM, GL_FLOAT, 0, &t[tshift]);
    )
  #endif
  }

EX void prepare(vector<colored_vertex>& v) {
  #if CAP_VERTEXBUFFER
  bindbuffer(v);
  PTR(glhr::aPosition, SHDIM, coords);
  PTR(glhr::aColor, 4, color);
  #else
  if(current_vertices == &v[0]) return;
  current_vertices = &v[0];
  WITHSHADER({
    glVertexAttribPointer(aPosition, SHDIM, GL_FLOAT, GL_FALSE, sizeof(colored_vertex), &v[0].coords);
    glVertexAttribPointer(aColor, 4, GL_FLOAT, GL_FALSE, sizeof(colored_vertex), &v[0].color);
    }, {
    glVertexPointer(SHDIM, GL_FLOAT, sizeof(colored_vertex), &v[0].coords);
    glColorPointer(4, GL_FLOAT, sizeof(colored_vertex), &v[0].color);
    })
  #endif
  }

EX void prepare(vector<textured_vertex>& v) {
  #if CAP_VERTEXBUFFER
  bindbuffer(v);
  PTR(glhr::aPosition, SHDIM, coords);
  PTR(glhr::aTexture, 2, texture);
  #else
  if(current_vertices == &v[0]) return;
  current_vertices = &v[0];
  WITHSHADER({
    glVertexAttribPointer(aPosition, SHDIM, GL_FLOAT, GL_FALSE, sizeof(textured_vertex), &v[0].coords);
    glVertexAttribPointer(aTexture, SHDIM, GL_FLOAT, GL_FALSE, sizeof(textured_vertex), &v[0].texture);
    }, {    
    glVertexPointer(SHDIM, GL_FLOAT, sizeof(textured_vertex), &v[0].coords);
    glTexCoordPointer(2, GL_FLOAT, sizeof(textured_vertex), &v[0].texture);
    })
  #endif
  // color2(col);
  }

EX void prepare(vector<ct_vertex>& v) {
  #if CAP_VERTEXBUFFER
  bindbuffer(v);
  PTR(glhr::aPosition, SHDIM, coords);
  PTR(glhr::aColor, 4, color);
  PTR(glhr::aTexture, 2, texture);
  #else
  if(current_vertices == &v[0]) return;
  current_vertices = &v[0];
  WITHSHADER({
    glVertexAttribPointer(aPosition, SHDIM, GL_FLOAT, GL_FALSE, sizeof(ct_vertex), &v[0].coords);
    glVertexAttribPointer(aColor, 4, GL_FLOAT, GL_FALSE, sizeof(ct_vertex), &v[0].color);
    glVertexAttribPointer(aTexture, 2, GL_FLOAT, GL_FALSE, sizeof(ct_vertex), &v[0].texture);
    }, {
    glVertexPointer(SHDIM, GL_FLOAT, sizeof(ct_vertex), &v[0].coords);
    glTexCoordPointer(2, GL_FLOAT, sizeof(ct_vertex), &v[0].texture);
    glColorPointer(4, GL_FLOAT, sizeof(ct_vertex), &v[0].color);
    })
  #endif
  }

EX void store_in_buffer(vector<glvertex>& v) {
#if CAP_VERTEXBUFFER
  if(!buf_buffered) {
    printf("no buffer yet\n");
    return;
    }
  printf("storing %d in buffer: %p\n", isize(v), &v[0]);
  current_vertices = buffered_vertices = &v[0];
  glBindBuffer(GL_ARRAY_BUFFER, buf_buffered);
  glVertexAttribPointer(glhr::aPosition, SHDIM, GL_FLOAT, GL_FALSE, sizeof(glvertex), 0);
  glBufferData(GL_ARRAY_BUFFER, isize(v) * sizeof(glvertex), &v[0], GL_STATIC_DRAW);
  printf("Stored.\n");
#endif
  }

EX void set_depthtest(bool b) {
  if(b != current_depthtest) {
    current_depthtest = b;
    if(b) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    }
  }


EX void set_depthwrite(bool b) {
  if(b != current_depthwrite) { // <- this does not work ask intended for some reason...
    current_depthwrite = b;
    if(b) glDepthMask(GL_TRUE);
    else glDepthMask(GL_FALSE);
    }
  }

EX void set_linewidth(ld lw) {
  if(lw != current_linewidth) {
    current_linewidth = lw;
    glLineWidth(lw);
    }
  }

EX void switch_to_text(const vector<glvertex>& v, const vector<glvertex>& t) {
  current_display->next_shader_flags = GF_TEXTURE;
  dynamicval<eModel> pm(pmodel, mdPixel);
  if(!svg::in) current_display->set_all(0);
  vertices_texture(v, t, 0, 0);
  }

EX void be_nontextured() { current_display->next_shader_flags = 0; }
EX void be_textured() { current_display->next_shader_flags = GF_TEXTURE; }

EX }

EX vector<glhr::textured_vertex> text_vertices;

EX void texture_vertices(GLfloat *f, int qty, int stride IS(2)) {
  WITHSHADER(
    glVertexAttribPointer(aTexture, stride, GL_FLOAT, GL_FALSE, stride * sizeof(GLfloat), f);,
    glTexCoordPointer(stride, GL_FLOAT, 0, f);
    )
  } 

EX void oldvertices(GLfloat *f, int qty) {
  WITHSHADER(
   glVertexAttribPointer(aPosition, SHDIM, GL_FLOAT, GL_FALSE, SHDIM * sizeof(GLfloat), f);,
   glVertexPointer(SHDIM, GL_FLOAT, 0, f);
   )
  }


}

#define glMatrixMode DISABLED
#define glLoadIdentity DISABLED
#define glMultMatrixf DISABLED
#define glScalef DISABLED
#define glTranslatef DISABLED
#define glPushMatrix DISABLED
#define glPopMatrix DISABLED

