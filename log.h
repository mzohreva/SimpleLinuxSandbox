#ifndef _LOG_D9E2673EFADA464D9659570557AD587E
#define _LOG_D9E2673EFADA464D9659570557AD587E

#include <string>
#include <fstream>

namespace util
{
    class SimpleLogStream
    {
      public:
        SimpleLogStream() : enabled{false}, log_file{""}, out_stream{nullptr}
        {
        }

        void SetOutput(std::ostream * out_stream_)
        {
            enabled = true;
            log_file = "";
            out_stream = out_stream_;
        }

        void SetOutput(std::string log_file_)
        {
            enabled = true;
            log_file = log_file_;
            out_stream = nullptr;
        }

        void Disable()
        {
            enabled = false;
            log_file = "";
            out_stream = nullptr;
        }

        template<typename T>
        void Write(const T& obj)
        {
            if (enabled)
            {
                if (out_stream)
                {
                    (*out_stream) << obj;
                }
                else
                {
                    std::ofstream ofs(log_file, std::ofstream::app);
                    ofs << obj;
                    ofs.close();
                }
            }
        }

      private:
        bool enabled;
        std::string log_file;
        std::ostream * out_stream;
    };

    template<typename T>
    SimpleLogStream& operator<<(SimpleLogStream& logger, const T& obj)
    {
        logger.Write(obj);
        return logger;
    }
}

#endif
