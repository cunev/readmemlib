#include <napi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>
#include <cstring>
#include <sys/ptrace.h>
#include <sys/wait.h>

using namespace Napi;

Napi::Value read_integer(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 2)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber() || !info[1].IsNumber())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  pid_t pid = info[0].As<Napi::Number>().Int32Value();
  unsigned long addr = info[1].As<Napi::Number>().Uint32Value();

  ptrace(PTRACE_INTERRUPT, pid, NULL, PTRACE_O_TRACEEXEC);
  std::stringstream ss;
  ss << "/proc/" << pid << "/mem";
  std::fstream fs(ss.str().c_str(), std::ios::in | std::ios::out | std::ios::binary);

  if (!fs.is_open())
  {
    Napi::Error::New(env, "Failed to open " + ss.str()).ThrowAsJavaScriptException();
    return env.Null();
  }

  fs.seekg(addr);
  char buf[4];
  fs.read(buf, 4);
  int value = *(int *)buf;
  fs.close();

  return Napi::Number::New(env, value);
}
Napi::Value write_integer(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  if (info.Length() < 3)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  pid_t pid = info[0].As<Napi::Number>().Int32Value();
  unsigned long addr = info[1].As<Napi::Number>().Uint32Value();
  int value = info[2].As<Napi::Number>().Int32Value();

  ptrace(PTRACE_ATTACH, pid, NULL, NULL);
  wait(NULL);
  std::stringstream ss;
  ss << "/proc/" << pid << "/mem";
  std::fstream fs(ss.str().c_str(), std::ios::in | std::ios::out | std::ios::binary);

  if (!fs.is_open())
  {
    Napi::Error::New(env, "Failed to open " + ss.str()).ThrowAsJavaScriptException();
    return env.Null();
  }

  fs.seekp(addr);
  fs.write((char *)&value, sizeof(value));
  fs.close();
  ptrace(PTRACE_DETACH, pid, NULL, NULL);
  return env.Null();
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
  exports.Set(Napi::String::New(env, "read_integer"),
              Napi::Function::New(env, read_integer));
  exports.Set(Napi::String::New(env, "write_integer"),
              Napi::Function::New(env, write_integer));
  return exports;
}

NODE_API_MODULE(addon, Init)
