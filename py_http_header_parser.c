#include <Python.h>
#include "structmember.h"
#include "deps/http-header/http_parser.c"
#include <sys/socket.h>
#include "py_http_header_parser.h"

/* Request Python __init__ method */
/* prepare initial object, setup parser struct. */
static int request_init(parser_data * self, PyObject * args, PyObject * kwargs){
  self->parser.data = self;
  http_parser_init(&self->parser, HTTP_REQUEST);
  // writeback
  Py_INCREF(Py_None);
  self->writeback = Py_None;
  // processor
  self->processor = NULL;
  // method
  Py_INCREF(Py_None);
  self->method = Py_None;
  // url
  Py_INCREF(Py_None);
  self->URI = Py_None;
  // Query
  Py_INCREF(Py_None);
  self->Query = Py_None;
  // header
  self->header = PyDict_New();
  // misc
  self->last_field = NULL;
  self->last_field_length = 0;
  self->message_complete = 0;
  self->header_complete = 0;
  self->error = 0;
  return 0;
}

/* Request Python __del__ method */
void request_del(parser_data * self){
  Py_DECREF(self->writeback);
  Py_XDECREF(self->processor);
  Py_DECREF(self->method);
  Py_DECREF(self->URI);
  Py_DECREF(self->Query);
  Py_DECREF(self->header);
  if(self->last_field != NULL)
    free(self->last_field);
  self->ob_type->tp_free((PyObject*)self);
}

/* Parser execution */
static int on_url(http_parser* p, const char *at, size_t length){
  // type casting
  parser_data * self = p->data;

  // method
  Py_DECREF(self->method);
  self->method = Py_BuildValue("s", http_method_str(p->method) );

  // URI
  Py_DECREF(self->URI);
  self->URI = Py_BuildValue("s#",at,length);

  // query
  size_t i = 0;
  while(i<length && at[i]!='?') i += 1;
  if(i<length){
    i += 1;
    Py_DECREF(self->Query);
    self->Query = Py_BuildValue("s#",at+i,length-i);
  }else{
    self->Query = Py_BuildValue("s","");
  }
  return 0;
}

static int on_header_field(http_parser* p, const char *at, size_t length) {
  // type casting
  parser_data * self = p->data;

  // free last stored field
  if(self->last_field != NULL) free(self->last_field);

  // alloc new space
  self->last_field = malloc(sizeof(char)*length);

  // copy
  memcpy(self->last_field, at, sizeof(char)*length);
  self->last_field_length = length;

  return 0;
}

static int on_header_value(http_parser* p, const char *at, size_t length) {
  // type casting
  parser_data * data = p->data;

  // setup insertion
  PyObject * key = Py_BuildValue("s#", data->last_field, data->last_field_length);
  PyObject * value = Py_BuildValue("s#",at,length);

  // insert
  PyDict_SetItem(data->header, key, value);

  // free memory
  Py_DECREF(key);
  Py_DECREF(value);

  return 0;
}

/* OLD Procedure */
/* static int try_route(parser_data * self){
  if(!PyObject_HasAttrString(self,"routing")){ // no method
    //printf("No routing method\n");
    return 0;
  }
  PyObject * callback_return = PyObject_CallMethod(self, "routing", "");
  if(callback_return==Py_None){ // no routing
    Py_DECREF(callback_return);
    return 0;
  }else{
    self->processor = callback_return;
    return 1;
  }
}*/

static int on_headers_complete(http_parser* p) {
  // type casting
  parser_data * self = p->data;

  // update info
  self->header_complete = 1; // DEPRECATED

  // Python callback
  // routing => Get data handler/processor
  if(!PyObject_HasAttrString(self,"routing"))
    return 1;
  PyObject * callback_return = PyObject_CallMethod(self, "routing", "");
  if(callback_return==Py_None){ // no routing
    Py_DECREF(callback_return);
    return 2;
  }

  // update processor
  self->processor = callback_return;

  return 0;
}

static int on_message_complete(http_parser* p) {
  // type casting
  parser_data * self = p->data;

  // update info
  self->message_complete = 1; // DEPRECATED

  // Python callback, data transfer will be here

  // check if processor available
  if(self->processor==NULL) return 1;
  if(!PyObject_HasAttrString(self->processor,"process"))
    return 1;

  // execute processor.process
  PyObject * callback_return = PyObject_CallMethod(self->processor, "process", "O", self->writeback);

  // free memory
  if(callback_return==Py_None){
    // success
    Py_DECREF(callback_return);
    return 0;
  }else{
    // failure
    Py_DECREF(callback_return);
    return 2;
  }
}

/* Callback to other C method like POST multipart, PUT, etc */
/* call "write" method,
  if success must be return None,
  otherwise are considered an failure.
  */
static int on_body(http_parser* p, const char *at, size_t length) {
  // type casting
  parser_data * self = p->data;

  // check if processor has write method (receive body part)
  if(!PyObject_HasAttrString(self->processor,"write"))
    return 1;

  // execute write method (transfer data)
  PyObject * callback_return = PyObject_CallMethod(self->processor, "write", "s#", at,length);

  // free memory & check if successful
  if(callback_return==Py_None){
    // success
    Py_DECREF(callback_return);
    return 0;
  }else{
    // failure
    Py_DECREF(callback_return);
    return 2;
  }
}

/* Dummy function */
static int on_info(http_parser* p) {
  return 0;
}
static int on_data(http_parser* p, const char *at, size_t length) {
  return 0;
}

/* Parser Setup */
static http_parser_settings settings = {
  // INFO
  .on_message_begin = on_info,
  .on_headers_complete = on_headers_complete,
  .on_message_complete = on_message_complete,

  // DATA
  .on_header_field = on_header_field,
  .on_header_value = on_header_value,
  .on_url = on_url,
  .on_status = on_data,
  .on_body = on_body
};

static void on_error(parser_data * self){
  // get parser
  http_parser* p = &self->parser;

  // get error description
  int errno_ = p->http_errno;
  char * name = http_errno_name(errno_);
  char * desc = http_errno_description(errno_);

  // error callback
  // check if error is available
  if(!PyObject_HasAttrString((PyObject*)self,"on_error")){
    printf("Warning: No error method\n");
    return;
  }
  // execute error callback
  PyObject * callback_return = PyObject_CallMethod((PyObject*)self, "on_error", "iss", errno_, name, desc);
  // free memory
  Py_XDECREF(callback_return);
}

/* Parse request
return 0 if success */
static int C_request_write(parser_data * self, char * at, size_t length){
  // get parser
  http_parser* p = &self->parser;

  // execute
  int parsed = http_parser_execute(&self->parser, &settings, at, length);

  // check error
  if(parsed != length) return 1;
  if(p->http_errno != HPE_OK) return 2;

  return 0;
}

/* Set writeback */
/* All output will be send to client would call this mechanism
  example:
    request.set_writeback( con.sendall )
*/
static PyObject * set_writeback(parser_data * self, PyObject * writeback){
  // free old writeback
  Py_DECREF(self->writeback);

  // set new writeback
  Py_INCREF(writeback);
  self->writeback = writeback;

  Py_RETURN_NONE;
}

/* Write */
/* Write data to parser from PyString */
static PyObject* hardwrite(parser_data * self, PyObject* buffer){
  // get char
  char * at;
  size_t length;
  PyString_AsStringAndSize(buffer, &at, &length);

  // execute
  if(C_request_write(self, at,length)){
    // error occurred
    // call error callback
    on_error(self);
  }

  Py_RETURN_NONE;
}

/* Socket write */
/* C caller */
/* Write data to parser from socket file descriptor */
static void C_write_from_socket(parser_data * self, int fd){
  // setup
  int length;
  char buffer[MAX_BUFFER];
  http_parser* p = &self->parser;

  // check initial error
  if(p->http_errno != HPE_OK)
    on_error(self);

  // execute
  while(!self->message_complete){
    length = recv(fd,buffer,MAX_BUFFER,0);
    if(C_request_write(self, buffer, length)){
      // error occurred
      on_error(self);
      break;
    }
  }
}
/* Socket write */
/* Python caller */
static PyObject * write_from_socket(parser_data * self, PyObject * py_fd){
  // get socket file descriptor
  int fd = PyInt_AsLong(py_fd);

  // execute
  C_write_from_socket(self,fd);

  Py_RETURN_NONE;
}

/* DEPRECATED */
static PyObject * fetch_data(parser_data * self){
  // specification : receive socket fd
  //int fd = self->fd;
  //int buffer_length;
  //int parsed;
  //char buffer[MAX_BUFFER];
  http_parser* p = &self->parser;
  // prepare
  self->parser.data = self;
  // start
  if(p->http_errno != HPE_OK){ // error occurred
    on_error(self);
  }else{
    /*while(!self->message_complete){
      buffer_length = recv(fd,buffer,MAX_BUFFER,0);
      if(C_request_write(self, buffer, buffer_length)){
        break;
      }
    }*/
  }
  /*
  while(p->http_errno == HPE_OK){
    buffer_length = recv(fd,buffer,MAX_BUFFER,0);
    parsed = http_parser_execute(&self->parser, &settings, buffer, buffer_length);
    if(!buffer_length) break;
    if(parsed != buffer_length){
      //printf("ERROR DETECTED!!!\n");
      //self->error = 1;
      break;
    };
    if(p->http_errno != HPE_OK) break;
    //if(self->header_complete) break;
    if(self->message_complete) break;
  }*/
  Py_RETURN_NONE;
}

/* Get Error status */
static PyObject * is_error(parser_data * self){
  if(self->parser.http_errno == HPE_OK) Py_RETURN_FALSE;
  else Py_RETURN_TRUE;
}

static PyMemberDef parser_data_members[] = {
    {"method", T_OBJECT_EX, offsetof(parser_data, method), READONLY, "HTTP Method"},
    {"URI", T_OBJECT_EX, offsetof(parser_data, URI), READONLY, "Request URI"},
    {"Query", T_OBJECT_EX, offsetof(parser_data, Query), READONLY, "Request Query. Used in GET variables."},
    {"header", T_OBJECT_EX, offsetof(parser_data, header), READONLY, "HTTP Header"},
    {NULL}
};
static PyMethodDef parser_data_methods[] = {
    {"fetch_data", (PyCFunction)fetch_data, METH_NOARGS, "Fetching data, return parser_data object." }, // DEPRECATED
    {"write", (PyCFunction)hardwrite, METH_O, "Write request stream." },
    {"set_writeback", (PyCFunction)set_writeback, METH_O, "Set writeback." },
    {"write_from_socket", (PyCFunction)write_from_socket, METH_O, "Write request stream from socket fd." },
    {"is_error", (PyCFunction)is_error, METH_NOARGS, "Check if parser_data object is error." },
    {NULL}
};
static PyTypeObject parser_data_type = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "http_parser.request",             /*tp_name*/
    sizeof(parser_data), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)request_del,                         /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "HTTP setup",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    parser_data_methods,             /* tp_methods */
    parser_data_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)request_init,      /* tp_init */
    0,                         /* tp_alloc */
    PyType_GenericNew,                 /* tp_new */
};

static PyMethodDef methods[] = {
  {NULL}
};

int prepare(PyObject* m){
  if (PyType_Ready(&parser_data_type) < 0){
    printf("Type not ready\n");
    return 1;
  }
  Py_INCREF(&parser_data_type);
  PyModule_AddObject(m, "request", (PyObject *)&parser_data_type);
  return 0;
}

void inithttp_request(){
  PyObject* m = Py_InitModule3("http_request", methods,"HTTP/1.x request parser modules.");
  if (m == NULL)
    return;
  prepare(m);
}
