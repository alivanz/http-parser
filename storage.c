#ifndef _STORAGE
#define _STORAGE

#include "deps/fields.c"
#include "deps/urlencoded.c"
#include "deps/constant.h"
#include "storage.h"

/* content_type FIELD */
static int content_type_on_key(fields_t * p, char * at, int length){
  storage_t * self = p->data;
  if(chars_check("application/x-www-form-urlencoded", at,length)){
    /* Set encoding */
    self->valid = 1;
    self->encoding = encoding_urlencoded;
    Py_XDECREF(self->py_encoding);
    self->py_encoding = PyString_FromStringAndSize(at,length);
    /* Setup */
    urlencoded_t * setup = malloc(sizeof(urlencoded_t));
    urlencoded_setup(setup);
    setup->data = self;
    self->setup = setup;
  }else if(chars_check("multipart/form-data", at,length)){
    /* Set encoding */
    self->encoding = encoding_formdata;
    Py_XDECREF(self->py_encoding);
    self->py_encoding = PyString_FromStringAndSize(at,length);
    /* Setup */
  }else{
    return -1;
  }
  return 0;
}
static int content_type_on_param(fields_t * p, char * at, int length){
  //storage_t * self = p->data;
  //if(self->encoding==encoding_formdata){

  //}
  return 0;
}
static int content_type_on_param_value(fields_t * p, char * at, int length){
  //storage_t * self = p->data;
  return 0;
}
fields_action content_type_action = {
  .on_key = content_type_on_key,
  .on_param = content_type_on_param,
  .on_param_value = content_type_on_param_value
};

/* Constructor & Destructor */
static int storage_init(storage_t * self, PyObject * args, PyObject * kwargs){
  char * content_type;
  int content_type_length;
  char * kwlist[] = {"content_type",NULL};
  if(!PyArg_ParseTupleAndKeywords(args,kwargs,"s#",kwlist, &content_type,&content_type_length)){
    return -1;
  }

  /* Initial */
  self->py_encoding = NULL;
  self->setup = NULL;
  self->writeback = NULL;
  self->boundary = NULL;
  self->room = NULL;

  self->valid = 0;

  self->field_key = NULL;
  self->field_filename = NULL;
  self->field_content_type = NULL;

  /* content_type */
  fields_t f;
  fields_setup(&f);
  f.data = self;
  if( fields_execute(&f,&content_type_action, content_type,content_type_length) != content_type_length ){
    if(PyErr_Occurred()==NULL) PyErr_SetString(PyExc_TypeError,"invalid encoding (1)");
    return -1;
  }
  if(!self->valid){
    if(PyErr_Occurred()==NULL) PyErr_SetString(PyExc_TypeError,"invalid encoding");
    return -1;
  }

  return 0;
}
static void storage_del(storage_t * self){
  Py_XDECREF(self->py_encoding);
  if(self->setup != NULL) free(self->setup);
  Py_XDECREF(self->writeback);
  Py_XDECREF(self->boundary);
  Py_XDECREF(self->room);

  Py_XDECREF(self->field_key);
  Py_XDECREF(self->field_filename);
  Py_XDECREF(self->field_content_type);
  self->ob_type->tp_free(self);
}

/* Generic callback */
static int storage_on_key(storage_t * self, char* at, int length){
  Py_XDECREF(self->field_key);
  self->field_key = PyString_FromStringAndSize(at,length);
  return 0;
}
static int storage_on_type(storage_t * self, char* at, int length){
  Py_XDECREF(self->field_content_type);
  self->field_content_type = PyString_FromStringAndSize(at,length);
  return 0;
}
static int storage_on_filename(storage_t * self, char* at, int length){
  Py_XDECREF(self->field_filename);
  self->field_filename = PyString_FromStringAndSize(at,length);
  return 0;
}
static int storage_on_header_end(storage_t * self){
  /* Get Handler */
  if(self->field_key==NULL){
    PyErr_SetString(PyExc_KeyError,"key undefined.");
    return -1;
  }
  PyObject * handler = PyObject_CallMethod((PyObject*)self, "item","O",self->field_key);
  //PyObject * attr = PyObject_GetAttr((PyObject*)self, self->field_key);
  if(handler==NULL){
    //PyErr_SetObject(PyExc_KeyError,self->field_key);
    return -1;
  }
  /* Set filename */
  if(self->field_filename!=NULL && PyObject_HasAttrString(handler,"set_filename")){
    PyObject * result = PyObject_CallMethod(handler,"set_filename","O",self->field_filename);
    if(result==NULL) return -1;
    Py_DECREF(result);
  }
  /* Set Content-Type */
  if(self->field_content_type!=NULL && PyObject_HasAttrString(handler,"set_type")){
    PyObject * result = PyObject_CallMethod(handler,"set_type","O",self->field_content_type);
    if(result==NULL) return -1;
    Py_DECREF(result);
  }
  /* Store Handler */
  Py_XDECREF(self->room);
  self->room = handler;
  /* Cleaning */
  Py_XDECREF(self->field_key);
  Py_XDECREF(self->field_filename);
  Py_XDECREF(self->field_content_type);
  return 0;
}
static int storage_on_data(storage_t * self, char* at, int length){
  PyObject * result = PyObject_CallMethod((PyObject*)self->room,"write","s#", at,length);
  if(result==NULL){
    return -1;
  }
  Py_DECREF(result);
  return 0;
}
static int storage_on_data_end(storage_t * self){
  if(PyObject_HasAttrString((PyObject*)self->room,"close")){
    PyObject * result = PyObject_CallMethod((PyObject*)self->room,"close","");
    if(result==NULL){
      return -1;
    }
    Py_DECREF(result);
  }
  return 0;
}

/* urlencoded callback */
static int storage_urlencoded_on_key(urlencoded_t * p, char* at, int length){
  printf("on_key\n");
  storage_t * self = p->data;
  /* ON KEY */
  if(storage_on_key(self, at,length)) return -1;
  /* ON HEADER END */
  return storage_on_header_end(self);
}
static int storage_urlencoded_on_data(urlencoded_t * p, char* at, int length){
  printf("on_data\n");
  return storage_on_data(p->data, at,length);
}
static int storage_urlencoded_on_data_end(urlencoded_t * p){
  printf("on_data_end\n");
  return storage_on_data_end(p->data);
}
static urlencoded_action storage_urlencoded_action = {
  .on_key = storage_urlencoded_on_key,
  .on_data = storage_urlencoded_on_data,
  .on_data_end = storage_urlencoded_on_data_end
};

/* multipart callback */
// pass---------------------

/* interface */
static PyObject * storage_set_writeback(storage_t * self, PyObject * writeback){
  if(!PyCallable_Check(writeback)){
    PyErr_SetString(PyExc_TypeError,"writeback is not callable.");
    return NULL;
  }
  Py_XDECREF(self->writeback);
  Py_INCREF(writeback);
  self->writeback = writeback;
  Py_RETURN_NONE;
}
static PyObject * storage_write(storage_t * self, PyObject * buffer){
  if(!PyString_CheckExact(buffer)){
    PyErr_SetString(PyExc_KeyError,"buffer must be str");
    return NULL;
  }
  char * at = PyString_AsString(buffer);
  int length = PyString_Size(buffer);

  if(self->encoding==encoding_urlencoded){
    int parsed = urlencoded_execute(self->setup, &storage_urlencoded_action, at,length);
    if(PyErr_Occurred()!=NULL) return NULL;
    if(parsed != length){
      PyErr_SetString(PyExc_ValueError,"parse error");
      return NULL;
    }
  }else if(self->encoding==encoding_formdata){
    //Py_RETURN_NONE;
  }else{
    PyErr_SetString(PyExc_RuntimeError,"unknown encoding");
    return NULL;
  }
  Py_RETURN_NONE;
}
static PyObject * storage_close(storage_t * self){
  if(self->encoding==encoding_urlencoded){
    if(urlencoded_execute(self->setup, &storage_urlencoded_action,NULL,0)){
      if(PyErr_Occurred()==NULL)
        PyErr_SetString(PyExc_ValueError,"parse error");
      return NULL;
    }
  }else if(self->encoding==encoding_formdata){
    // -----
  }else{
    PyErr_SetString(PyExc_RuntimeError,"unknown encoding");
    return NULL;
  }
  if(storage_on_data_end(self)){
    return NULL;
  }
  Py_RETURN_NONE;
}

/* MAPPING */
static PyMemberDef storage_members[] = {
  {"writeback", T_OBJECT, offsetof(storage_t, writeback), READONLY, "writeback. Setup by storage.set_writeback(writeback)"},
  {"encoding", T_OBJECT, offsetof(storage_t, py_encoding), READONLY, "encoding."},
  {NULL}
};
static PyMethodDef storage_methods[] = {
  //{"set_writeback", (PyCFunction)set_writeback, METH_O, "Set writeback." },
  {"set_writeback", (PyCFunction)storage_set_writeback, METH_O, "Set writeback." },
  {"write", (PyCFunction)storage_write, METH_O, "Write." },
  {"close", (PyCFunction)storage_close, METH_NOARGS, "Close." },
  {NULL}
};

static PyTypeObject storage_type = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "alivanz.storage",             /*tp_name*/
    sizeof(storage_t), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)storage_del,                         /*tp_dealloc*/
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
    "Transfer Storage.\n"
    "storage(content_type)\n"
    "\n"
    "Supported encoding:\n"
    "\tapplication/x-www-form-urlencoded\n"
    "\tmultipart/form-data (under construction)\n",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    storage_methods,             /* tp_methods */
    storage_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)storage_init,      /* tp_init */
    0,                         /* tp_alloc */
    PyType_GenericNew,                 /* tp_new */
};
int storage_prepare(PyObject* m){
  if (PyType_Ready(&storage_type) < 0){
    printf("Storage not ready\n");
    return 1;
  }
  Py_INCREF(&storage_type);

  PyModule_AddObject(m, "storage", (PyObject *)&storage_type);
  return 0;
}

#endif
