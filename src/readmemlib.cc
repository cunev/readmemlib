#include <napi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <string>
#include <cstring>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <thread>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

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

  ptrace(PTRACE_INTERRUPT, pid, NULL, PTRACE_O_TRACEEXEC);
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
Napi::Value get_pid_from_window_title(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 1)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string window_title = info[0].As<Napi::String>();

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
  {
    Napi::Error::New(env, "Failed to open X display").ThrowAsJavaScriptException();
    return env.Null();
  }

  Window root = DefaultRootWindow(display);
  Atom property = XInternAtom(display, "_NET_CLIENT_LIST", true);
  Atom actual_type;
  int actual_format;
  unsigned long num_windows, bytes_remaining;
  unsigned char *data = NULL;

  if (XGetWindowProperty(display, root, property, 0, (~0L), false, AnyPropertyType,
                         &actual_type, &actual_format, &num_windows, &bytes_remaining, &data) != Success)
  {
    Napi::Error::New(env, "Failed to get window list").ThrowAsJavaScriptException();
    XCloseDisplay(display);
    return env.Null();
  }

  Window *window_list = (Window *)data;
  pid_t pid = -1;

  for (unsigned long i = 0; i < num_windows; ++i)
  {
    Window window = window_list[i];
    char *name;
    if (XFetchName(display, window, &name) > 0)
    {
      std::string current_title(name);
      XFree(name);

      if (current_title == window_title)
      {
        Atom pid_property = XInternAtom(display, "_NET_WM_PID", true);
        Atom pid_type;
        int pid_format;
        unsigned long pid_nitems, pid_bytes_remaining;
        unsigned char *pid_data = NULL;

        if (XGetWindowProperty(display, window, pid_property, 0, 1, false, XA_CARDINAL,
                               &pid_type, &pid_format, &pid_nitems, &pid_bytes_remaining, &pid_data) == Success)
        {
          if (pid_data != NULL)
          {
            pid = *((pid_t *)pid_data);
            XFree(pid_data);
            break;
          }
        }
      }
    }
  }

  XFree(window_list);
  XCloseDisplay(display);

  if (pid != -1)
  {
    return Napi::Number::New(env, pid);
  }
  else
  {
    Napi::Error::New(env, "Window with the specified title not found").ThrowAsJavaScriptException();
    return env.Null();
  }
}

Napi::Value get_pids_from_partial_title(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 1)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string partial_title = info[0].As<Napi::String>();

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
  {
    Napi::Error::New(env, "Failed to open X display").ThrowAsJavaScriptException();
    return env.Null();
  }

  Window root = DefaultRootWindow(display);
  Atom property = XInternAtom(display, "_NET_CLIENT_LIST", true);
  Atom actual_type;
  int actual_format;
  unsigned long num_windows, bytes_remaining;
  unsigned char *data = NULL;

  if (XGetWindowProperty(display, root, property, 0, (~0L), false, AnyPropertyType,
                         &actual_type, &actual_format, &num_windows, &bytes_remaining, &data) != Success)
  {
    Napi::Error::New(env, "Failed to get window list").ThrowAsJavaScriptException();
    XCloseDisplay(display);
    return env.Null();
  }

  Window *window_list = (Window *)data;
  Napi::Array result = Napi::Array::New(env);

  for (unsigned long i = 0; i < num_windows; ++i)
  {
    Window window = window_list[i];
    char *name;
    if (XFetchName(display, window, &name) > 0)
    {
      std::string current_title(name);
      XFree(name);

      if (current_title.find(partial_title) != std::string::npos)
      {
        Atom pid_property = XInternAtom(display, "_NET_WM_PID", true);
        Atom classname_property = XInternAtom(display, "WM_CLASS", true);
        Atom pid_type, classname_type;
        int pid_format, classname_format;
        unsigned long pid_nitems, classname_nitems, pid_bytes_remaining, classname_bytes_remaining;
        unsigned char *pid_data = NULL, *classname_data = NULL;

        if (XGetWindowProperty(display, window, pid_property, 0, 1, false, XA_CARDINAL,
                               &pid_type, &pid_format, &pid_nitems, &pid_bytes_remaining, &pid_data) == Success &&
            XGetWindowProperty(display, window, classname_property, 0, (~0L), false, XA_STRING,
                               &classname_type, &classname_format, &classname_nitems, &classname_bytes_remaining, &classname_data) == Success)
        {
          if (pid_data != NULL && classname_data != NULL)
          {
            pid_t pid = *((pid_t *)pid_data);
            std::string classname(reinterpret_cast<char *>(classname_data));

            Napi::Object entry = Napi::Object::New(env);
            entry.Set("pid", Napi::Number::New(env, pid));
            entry.Set("title", Napi::String::New(env, current_title));
            entry.Set("classname", Napi::String::New(env, classname));

            result.Set(result.Length(), entry);
            XFree(pid_data);
            XFree(classname_data);
          }
        }
      }
    }
  }

  XFree(window_list);
  XCloseDisplay(display);

  return result;
}

Napi::Value get_window_title_by_pid(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 1)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  pid_t target_pid = info[0].As<Napi::Number>().Int32Value();

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
  {
    Napi::Error::New(env, "Failed to open X display").ThrowAsJavaScriptException();
    return env.Null();
  }

  Window root = DefaultRootWindow(display);
  Atom property = XInternAtom(display, "_NET_CLIENT_LIST", true);
  Atom actual_type;
  int actual_format;
  unsigned long num_windows, bytes_remaining;
  unsigned char *data = NULL;

  if (XGetWindowProperty(display, root, property, 0, (~0L), false, AnyPropertyType,
                         &actual_type, &actual_format, &num_windows, &bytes_remaining, &data) != Success)
  {
    Napi::Error::New(env, "Failed to get window list").ThrowAsJavaScriptException();
    XCloseDisplay(display);
    return env.Null();
  }

  Window *window_list = (Window *)data;
  std::string window_title = "";

  for (unsigned long i = 0; i < num_windows; ++i)
  {
    Window window = window_list[i];
    Atom pid_property = XInternAtom(display, "_NET_WM_PID", true);
    Atom pid_type;
    int pid_format;
    unsigned long pid_nitems, pid_bytes_remaining;
    unsigned char *pid_data = NULL;

    if (XGetWindowProperty(display, window, pid_property, 0, 1, false, XA_CARDINAL,
                           &pid_type, &pid_format, &pid_nitems, &pid_bytes_remaining, &pid_data) == Success)
    {
      if (pid_data != NULL)
      {
        pid_t pid = *((pid_t *)pid_data);
        XFree(pid_data);

        if (pid == target_pid)
        {
          char *name;
          if (XFetchName(display, window, &name) > 0)
          {
            window_title = std::string(name);
            XFree(name);
            break;
          }
        }
      }
    }
  }

  XFree(window_list);
  XCloseDisplay(display);

  if (!window_title.empty())
  {
    return Napi::String::New(env, window_title);
  }
  else
  {
    Napi::Error::New(env, "Window with the specified PID not found").ThrowAsJavaScriptException();
    return env.Null();
  }
}

Napi::Value disable_window_input(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 1)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  pid_t target_pid = info[0].As<Napi::Number>().Int32Value();

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
  {
    Napi::Error::New(env, "Failed to open X display").ThrowAsJavaScriptException();
    return env.Null();
  }

  Window root = DefaultRootWindow(display);
  Atom property = XInternAtom(display, "_NET_CLIENT_LIST", true);
  Atom actual_type;
  int actual_format;
  unsigned long num_windows, bytes_remaining;
  unsigned char *data = NULL;

  if (XGetWindowProperty(display, root, property, 0, (~0L), false, AnyPropertyType,
                         &actual_type, &actual_format, &num_windows, &bytes_remaining, &data) != Success)
  {
    Napi::Error::New(env, "Failed to get window list").ThrowAsJavaScriptException();
    XCloseDisplay(display);
    return env.Null();
  }

  Window *window_list = (Window *)data;

  for (unsigned long i = 0; i < num_windows; ++i)
  {
    Window window = window_list[i];
    Atom pid_property = XInternAtom(display, "_NET_WM_PID", true);
    Atom pid_type;
    int pid_format;
    unsigned long pid_nitems, pid_bytes_remaining;
    unsigned char *pid_data = NULL;

    if (XGetWindowProperty(display, window, pid_property, 0, 1, false, XA_CARDINAL,
                           &pid_type, &pid_format, &pid_nitems, &pid_bytes_remaining, &pid_data) == Success)
    {
      if (pid_data != NULL)
      {
        pid_t pid = *((pid_t *)pid_data);
        XFree(pid_data);

        if (pid == target_pid)
        {
          // Disable input for the window
          XSelectInput(display, window, 0);

          // Make the window passthrough
          XserverRegion region = XFixesCreateRegion(display, NULL, 0);
          XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, region);
          XFixesDestroyRegion(display, region);

          break;
        }
      }
    }
  }

  XFree(window_list);
  XCloseDisplay(display);

  return env.Null();
}

Napi::Value enable_window_input(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 1)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  pid_t target_pid = info[0].As<Napi::Number>().Int32Value();

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
  {
    Napi::Error::New(env, "Failed to open X display").ThrowAsJavaScriptException();
    return env.Null();
  }

  Window root = DefaultRootWindow(display);
  Atom property = XInternAtom(display, "_NET_CLIENT_LIST", true);
  Atom actual_type;
  int actual_format;
  unsigned long num_windows, bytes_remaining;
  unsigned char *data = NULL;

  if (XGetWindowProperty(display, root, property, 0, (~0L), false, AnyPropertyType,
                         &actual_type, &actual_format, &num_windows, &bytes_remaining, &data) != Success)
  {
    Napi::Error::New(env, "Failed to get window list").ThrowAsJavaScriptException();
    XCloseDisplay(display);
    return env.Null();
  }

  Window *window_list = (Window *)data;

  for (unsigned long i = 0; i < num_windows; ++i)
  {
    Window window = window_list[i];
    Atom pid_property = XInternAtom(display, "_NET_WM_PID", true);
    Atom pid_type;
    int pid_format;
    unsigned long pid_nitems, pid_bytes_remaining;
    unsigned char *pid_data = NULL;

    if (XGetWindowProperty(display, window, pid_property, 0, 1, false, XA_CARDINAL,
                           &pid_type, &pid_format, &pid_nitems, &pid_bytes_remaining, &pid_data) == Success)
    {
      if (pid_data != NULL)
      {
        pid_t pid = *((pid_t *)pid_data);
        XFree(pid_data);

        if (pid == target_pid)
        {
          // Enable input for the window
          XSelectInput(display, window, StructureNotifyMask | SubstructureNotifyMask);

          // Reset the input shape
          XShapeCombineMask(display, window, ShapeInput, 0, 0, None, ShapeSet);

          break;
        }
      }
    }
  }

  XFree(window_list);
  XCloseDisplay(display);

  return env.Null();
}

Napi::Value make_window_topmost(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 1)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  pid_t target_pid = info[0].As<Napi::Number>().Int32Value();

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
  {
    Napi::Error::New(env, "Failed to open X display").ThrowAsJavaScriptException();
    return env.Null();
  }

  Window root = DefaultRootWindow(display);
  Atom property = XInternAtom(display, "_NET_CLIENT_LIST", true);
  Atom actual_type;
  int actual_format;
  unsigned long num_windows, bytes_remaining;
  unsigned char *data = NULL;

  if (XGetWindowProperty(display, root, property, 0, (~0L), false, AnyPropertyType,
                         &actual_type, &actual_format, &num_windows, &bytes_remaining, &data) != Success)
  {
    Napi::Error::New(env, "Failed to get window list").ThrowAsJavaScriptException();
    XCloseDisplay(display);
    return env.Null();
  }

  Window *window_list = (Window *)data;

  for (unsigned long i = 0; i < num_windows; ++i)
  {
    Window window = window_list[i];
    Atom pid_property = XInternAtom(display, "_NET_WM_PID", true);
    Atom pid_type;
    int pid_format;
    unsigned long pid_nitems, pid_bytes_remaining;
    unsigned char *pid_data = NULL;

    if (XGetWindowProperty(display, window, pid_property, 0, 1, false, XA_CARDINAL,
                           &pid_type, &pid_format, &pid_nitems, &pid_bytes_remaining, &pid_data) == Success)
    {
      if (pid_data != NULL)
      {
        pid_t pid = *((pid_t *)pid_data);
        XFree(pid_data);

        if (pid == target_pid)
        {
          // Make the window topmost
          XRaiseWindow(display, window);
          break;
        }
      }
    }
  }

  XFree(window_list);
  XCloseDisplay(display);

  return env.Null();
}

static void destroy_window_cb(GtkWidget *widget, GtkWidget *window)
{
  gtk_main_quit();
}

Napi::Value create_browser_window(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 1)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string url = info[0].As<Napi::String>().Utf8Value();

  std::thread([url]
              {
 gtk_init(0, NULL);

    // Create a GTK window
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(destroy_window_cb), NULL);
    gtk_window_set_title(GTK_WINDOW(window),"overlay-next-ag");
    // Set window to fullscreen
    // gtk_window_fullscreen(GTK_WINDOW(window));

             // Set window to borderless
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

                // Set window position to (0, 0)
    gtk_window_set_default_size(GTK_WINDOW(window), 1, 1);
    gtk_window_move(GTK_WINDOW(window), 0, 0);
    // Set window to transparent
    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);

    if (visual != NULL && gdk_screen_is_composited(screen))
    {
        gtk_widget_set_visual(window, visual);
        gtk_widget_set_app_paintable(window, TRUE);
    }

    // Create a WebKit web view and add it to the window
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(web_view));

    // Set WebKitWebView background to transparent
    GdkRGBA rgba = { .alpha = 0.0 };
    webkit_web_view_set_background_color(web_view, &rgba);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(web_view));

    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    // Load the specified URL
    webkit_web_view_load_uri(web_view, url.c_str());

    // Show the window and run the GTK main loop
    gtk_widget_show_all(window);
    gtk_main(); })
      .detach();

  return env.Null();
}

Napi::Value set_window_size_by_pid(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 2)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  pid_t target_pid = info[0].As<Napi::Number>().Int32Value();
  int new_height = info[1].As<Napi::Number>().Int32Value();
  int new_width = info[2].As<Napi::Number>().Int32Value();

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
  {
    Napi::Error::New(env, "Failed to open X display").ThrowAsJavaScriptException();
    return env.Null();
  }

  Window root = DefaultRootWindow(display);
  Atom property = XInternAtom(display, "_NET_CLIENT_LIST", true);
  Atom actual_type;
  int actual_format;
  unsigned long num_windows, bytes_remaining;
  unsigned char *data = NULL;

  if (XGetWindowProperty(display, root, property, 0, (~0L), false, AnyPropertyType,
                         &actual_type, &actual_format, &num_windows, &bytes_remaining, &data) != Success)
  {
    Napi::Error::New(env, "Failed to get window list").ThrowAsJavaScriptException();
    XCloseDisplay(display);
    return env.Null();
  }

  Window *window_list = (Window *)data;

  for (unsigned long i = 0; i < num_windows; ++i)
  {
    Window window = window_list[i];
    Atom pid_property = XInternAtom(display, "_NET_WM_PID", true);
    Atom pid_type;
    int pid_format;
    unsigned long pid_nitems, pid_bytes_remaining;
    unsigned char *pid_data = NULL;

    if (XGetWindowProperty(display, window, pid_property, 0, 1, false, XA_CARDINAL,
                           &pid_type, &pid_format, &pid_nitems, &pid_bytes_remaining, &pid_data) == Success)
    {
      if (pid_data != NULL)
      {
        pid_t pid = *((pid_t *)pid_data);
        XFree(pid_data);

        if (pid == target_pid)
        {
          // Get the current geometry of the window
          XWindowAttributes attrs;
          XGetWindowAttributes(display, window, &attrs);

          // Set the window height to the new value
          XMoveResizeWindow(display, window, attrs.x, attrs.y, new_width, new_height);
          break;
        }
      }
    }
  }

  XFree(window_list);
  XCloseDisplay(display);

  return env.Null();
}

Napi::Value get_async_key_state(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 1)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  int key_code = info[0].As<Napi::Number>().Int32Value();

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
  {
    Napi::Error::New(env, "Failed to open X display").ThrowAsJavaScriptException();
    return env.Null();
  }

  char keys_return[32];
  XQueryKeymap(display, keys_return);
  KeyCode kc = XKeysymToKeycode(display, key_code);
  bool is_pressed = (keys_return[kc / 8] & (1 << (kc % 8))) != 0;

  XCloseDisplay(display);

  return Napi::Boolean::New(env, is_pressed);
}

Napi::Object get_screen_size(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
  {
    Napi::Error::New(env, "Failed to open X display").ThrowAsJavaScriptException();
    return Napi::Object::New(env);
  }

  int screen_num = DefaultScreen(display);
  int screen_width = DisplayWidth(display, screen_num);
  int screen_height = DisplayHeight(display, screen_num);

  XCloseDisplay(display);

  Napi::Object result = Napi::Object::New(env);
  result.Set("width", Napi::Number::New(env, screen_width));
  result.Set("height", Napi::Number::New(env, screen_height));

  return result;
}

bool compare_bytes(const unsigned char *data, const std::vector<unsigned char> &signature, const std::vector<bool> &mask)
{
  for (size_t i = 0; i < signature.size(); ++i)
  {
    if (!mask[i] && data[i] != signature[i])
    {
      return false;
    }
  }
  return true;
}

Napi::Value sigscan(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 3)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsString())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  pid_t pid = info[0].As<Napi::Number>().Int32Value();
  unsigned long start_addr = info[1].As<Napi::Number>().Uint32Value();
  std::string sig_str = info[2].As<Napi::String>().Utf8Value();

  std::vector<unsigned char> signature;
  std::vector<bool> mask;
  std::istringstream ss(sig_str);
  std::string byte_str;
  while (std::getline(ss, byte_str, ' '))
  {
    if (byte_str == "??")
    {
      signature.push_back(0);
      mask.push_back(true);
    }
    else
    {
      signature.push_back(static_cast<unsigned char>(std::stoi(byte_str, nullptr, 16)));
      mask.push_back(false);
    }
  }

  ptrace(PTRACE_INTERRUPT, pid, NULL, PTRACE_O_TRACEEXEC);
  std::stringstream mem_ss;
  mem_ss << "/proc/" << pid << "/mem";
  std::fstream fs(mem_ss.str().c_str(), std::ios::in | std::ios::out | std::ios::binary);

  if (!fs.is_open())
  {
    Napi::Error::New(env, "Failed to open " + mem_ss.str()).ThrowAsJavaScriptException();
    return env.Null();
  }

  fs.seekg(start_addr);

  const size_t buffer_size = 4096;
  unsigned char buffer[buffer_size];
  unsigned long address = 0;
  bool found = false;

  while (fs.read(reinterpret_cast<char *>(buffer), buffer_size))
  {
    for (size_t i = 0; i < buffer_size - signature.size() + 1; ++i)
    {
      if (compare_bytes(buffer + i, signature, mask))
      {
        found = true;
        address += i;
        break;
      }
    }

    if (found)
    {
      break;
    }

    address += buffer_size - signature.size() + 1;
    fs.seekg(start_addr + address);
  }
  fs.close();

  if (found)
  {
    return Napi::Number::New(env, start_addr + address);
  }
  else
  {
    return env.Null();
  }
}

void messageBox(const std::string &title, const std::string &message)
{
  gtk_init(0, NULL);

  GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                             GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_MESSAGE_INFO,
                                             GTK_BUTTONS_OK,
                                             "%s",
                                             message.c_str());
  gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);

  while (g_main_context_iteration(NULL, FALSE))
    ;
}

Napi::Value show_message_box(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 2)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString() || !info[1].IsString())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string title = info[0].As<Napi::String>().Utf8Value();
  std::string message = info[1].As<Napi::String>().Utf8Value();

  messageBox(title, message);

  return env.Null();
}

std::string showInputDialog(const std::string &title, const std::string &message)
{
  gtk_init(0, NULL);

  GtkWidget *dialog = gtk_dialog_new_with_buttons(title.c_str(),
                                                  NULL,
                                                  static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                                                  "_OK",
                                                  GTK_RESPONSE_ACCEPT,
                                                  "_Cancel",
                                                  GTK_RESPONSE_REJECT,
                                                  NULL);

  GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *label = gtk_label_new(message.c_str());
  GtkWidget *entry = gtk_entry_new();

  gtk_container_add(GTK_CONTAINER(content_area), label);
  gtk_container_add(GTK_CONTAINER(content_area), entry);

  gtk_widget_show_all(dialog);

  std::string result;
  gint response = gtk_dialog_run(GTK_DIALOG(dialog));
  if (response == GTK_RESPONSE_ACCEPT)
  {
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
    result = std::string(text);
  }

  gtk_widget_destroy(dialog);

  while (g_main_context_iteration(NULL, FALSE))
    ;

  return result;
}

Napi::Value get_input_dialog(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();

  if (info.Length() < 2)
  {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString() || !info[1].IsString())
  {
    Napi::TypeError::New(env, "Wrong argument types").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string title = info[0].As<Napi::String>().Utf8Value();
  std::string message = info[1].As<Napi::String>().Utf8Value();

  std::string result = showInputDialog(title, message);

  return Napi::String::New(env, result);
}

std::string getComputerId()
{
  std::string product_uuid;
  std::string product_serial;

  std::ifstream uuid_file("/sys/class/dmi/id/product_uuid");
  if (uuid_file)
  {
    std::getline(uuid_file, product_uuid);
    uuid_file.close();
  }

  std::ifstream serial_file("/sys/class/dmi/id/product_serial");
  if (serial_file)
  {
    std::getline(serial_file, product_serial);
    serial_file.close();
  }

  std::stringstream ss;
  ss << product_uuid << product_serial;
  return ss.str();
}

Napi::Value computer_id(const Napi::CallbackInfo &info)
{
  Napi::Env env = info.Env();
  std::string computer_id = getComputerId();
  return Napi::String::New(env, computer_id);
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
  exports.Set(Napi::String::New(env, "read_integer"),
              Napi::Function::New(env, read_integer));
  exports.Set(Napi::String::New(env, "write_integer"),
              Napi::Function::New(env, write_integer));
  exports.Set(Napi::String::New(env, "get_pid_from_window_title"),
              Napi::Function::New(env, get_pid_from_window_title));
  exports.Set(Napi::String::New(env, "get_pids_from_partial_title"),
              Napi::Function::New(env, get_pids_from_partial_title));
  exports.Set(Napi::String::New(env, "get_window_title_by_pid"),
              Napi::Function::New(env, get_window_title_by_pid));
  exports.Set(Napi::String::New(env, "disable_window_input"),
              Napi::Function::New(env, disable_window_input));
  exports.Set(Napi::String::New(env, "enable_window_input"),
              Napi::Function::New(env, enable_window_input));
  exports.Set(Napi::String::New(env, "make_window_topmost"),
              Napi::Function::New(env, make_window_topmost));
  exports.Set(Napi::String::New(env, "create_browser_window"),
              Napi::Function::New(env, create_browser_window));
  exports.Set(Napi::String::New(env, "set_window_size_by_pid"),
              Napi::Function::New(env, set_window_size_by_pid));
  exports.Set(Napi::String::New(env, "get_async_key_state"),
              Napi::Function::New(env, get_async_key_state));
  exports.Set(Napi::String::New(env, "sigscan"),
              Napi::Function::New(env, sigscan));
  exports.Set(Napi::String::New(env, "get_screen_size"),
              Napi::Function::New(env, get_screen_size));
  exports.Set(Napi::String::New(env, "show_message_box"),
              Napi::Function::New(env, show_message_box));
  exports.Set(Napi::String::New(env, "get_input_dialog"),
              Napi::Function::New(env, get_input_dialog));
  exports.Set(Napi::String::New(env, "computer_id"),
              Napi::Function::New(env, computer_id));
  return exports;
}

NODE_API_MODULE(addon, Init)