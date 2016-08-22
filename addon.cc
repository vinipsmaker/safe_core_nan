#include <nan.h>

#include <thread>
#include <chrono>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <cstdint>

// safe_core API {{{

extern "C" {
    std::int32_t create_account(const char *c_account_locator,
                                const char *c_account_passwor,
                                void **ffi_handle);
}

// }}}

/* JavaScript doesn't have a memory model and it's very unsafe to work with it.

   NodeJS uses v8 VM to execute JavaScript code and it adds bindings to libuv
   native functions that will allow multiple concurrent asynchronous tasks to be
   performed in a single thread.

   NodeJS also has an API to write native modules.

   It's unsafe to access v8 objects in the background thread. Therefore, we
   should convert (if possible) as many args as possible to C++ objects before
   spawning the helper thread. Objects that cannot be converted such as callback
   functions should be stored in `Persistent` handles to protect them from the
   GC and later be accessed again from the main thread. Using libuv it's
   possible to send execution units from other threads to be executed at some
   point and the pattern described here becomes possible.

   nan is a library of Native Abstractions for NodeJS. Its purpose is to
   decrease boilerplate and abstract differences among v8 versions. v8 breaks
   its API **VERY** often (believe me, I have been facing this demon elsewhere
   in the past) and thus it's very important we use something along these lines.

   nan exposes an `AsyncWorker` helper class that will spawn a thread, execute
   some work and then deliver result (also some work being executed) on the main
   thread. With such abstraction, we've built a model.

   Under our model, we spawn at most one thread at any given time. To make this
   work, we have a queue of jobs a `bool active_thread` variable. When we call a
   function to initiate an ascynchronous operation, we check the state of this
   variable:

   - If it is `false`: we set it to `true` and spawn the helper `AsyncWorker`
     object to execute the time consuming operation.
   - If it is `true`: we add the job to the `queue` variable.

   Note that these variables (`active_thread` and `queue`) are only accessed
   from the main thread and need no mutex to control their access.

   The `AsyncWorker` object  is responsible for:

   - On the secondary thread: Executing the time consuming operation.

   - On the main thread: when it's moved back to the main thread to deliver the
     operation result, it should check `queue.size()`. If it's `0`, we just set
     `active_thread` to `false`. Otherwise, we spawn another `AsyncWorker` on
     some pending operation. */

static bool active_thread = false;

enum ActionType {
    CreateAccountAsync
};

struct Args {
    std::vector<std::string> string_args;
};

struct Action {
    ActionType type;
    Args args;
    Nan::Callback callback;
};

std::deque<Action> queue;
std::mutex queue_mutex;

void create_account_async(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    v8::Local<v8::String> arg1 = info[0].As<v8::String>();
    v8::Local<v8::String> arg2 = info[1].As<v8::String>();
    v8::Local<v8::Function> cb = info[2].As<v8::Function>();
    const unsigned argc = 1;
    {
        Action a;
        a.type = CreateAccountAsync;
        {
            v8::String::Utf8Value data(arg1);
            a.args.string_args.push_back(std::string(*data, data.length()));
        }
        {
            v8::String::Utf8Value data(arg2);
            a.args.string_args.push_back(std::string(*data, data.length()));
        }
        a.callback.Reset(cb);
    }
    v8::Local<v8::Value> argv[argc] = { Nan::New("hello world").ToLocalChecked() };
    Nan::MakeCallback(Nan::GetCurrentContext()->Global(), cb, argc, argv);
}

void Init(v8::Local<v8::Object> exports, v8::Local<v8::Object> module) {
    std::thread([]() {
            for (;;) {
                std::unique_lock<std::mutex> guard(queue_mutex,
                                                   std::try_to_lock);
                if (!guard.owns_lock() || queue.size() == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                auto &a = queue.front();
                switch (a.type) {
                case CreateAccountAsync:
                    {
                        auto arg1 = a.args.string_args[0];
                        auto arg2 = a.args.string_args[0];
                        void *handle;
                        auto ret = create_account(arg1.c_str(), arg2.c_str(),
                                                  &handle);
                        
                    }
                }
                queue.pop_front();
            }
        }).detach();

    Nan::SetMethod(exports, "create_account_async", create_account_async);
    //Nan::SetMethod(module, "exports", RunCallback);
}

NODE_MODULE(safe_core_async, Init)
