/* parser.cc
 * libxml++ and this file are copyright (C) 2000 by Ari Johnson, and
 * are covered by the GNU Lesser General Public License, which should be
 * included with libxml++ as the file COPYING.
 */

#include "parsers/wrapped_exception.h"
#include "parsers/parse_error.h"
#include "parsers/parser.h"

#include <libxml/parser.h>

namespace xmlpp
{

struct Parser::Impl
{
  Impl()
  :
  throw_messages_(true), set_options_(0), clear_options_(0)
  {}

  // Built gradually - used in an exception at the end of parsing.
  std::string parser_error_;
  std::string parser_warning_;


  bool throw_messages_;
  int set_options_;
  int clear_options_;
};

Parser::Parser()
: context_(nullptr), exception_(nullptr), pimpl_(new Impl)
{
}

Parser::~Parser()
{
  release_underlying();
}



void Parser::set_throw_messages(bool val) noexcept
{
  pimpl_->throw_messages_ = val;
}

bool Parser::get_throw_messages() const noexcept
{
  return pimpl_->throw_messages_;
}

void Parser::set_parser_options(int set_options, int clear_options) noexcept
{
  pimpl_->set_options_ = set_options;
  pimpl_->clear_options_ = clear_options;
}

void Parser::get_parser_options(int& set_options, int& clear_options) const noexcept
{
  set_options = pimpl_->set_options_;
  clear_options = pimpl_->clear_options_;
}

void Parser::initialize_context()
{
  //Clear these temporary buffers:
  pimpl_->parser_error_.erase();
  pimpl_->parser_warning_.erase();

  //Disactivate any non-standards-compliant libxml1 features.
  //These are deactivated by default, but if we don't deactivate them for each context
  //then some other code which uses a global function, such as xmlKeepBlanksDefault(),
  // could cause this to use the wrong settings:
  context_->linenumbers = 1; // TRUE - This is the default anyway.

  //Turn on/off validation, entity substitution and default attribute inclusion.
  int options = context_->options;

  options &= ~XML_PARSE_DTDVALID;
  options &= ~XML_PARSE_DTDATTR;

  options |= XML_PARSE_NONET | XML_PARSE_NOENT;

  //Turn on/off any parser options.
  options |= pimpl_->set_options_;
  options &= ~pimpl_->clear_options_;

  xmlCtxtUseOptions(context_, options);

  if (context_->sax && pimpl_->throw_messages_)
  {
    //Tell the parser context about the callbacks.
    context_->sax->fatalError = &callback_parser_error;
    context_->sax->error = &callback_parser_error;
    context_->sax->warning = &callback_parser_warning;
  }

  //Allow callback_error_or_warning() to retrieve the C++ instance:
  context_->_private = this;
}

void Parser::release_underlying()
{
  if(context_)
  {
    context_->_private = nullptr; //Not really necessary.

    xmlFreeParserCtxt(context_);
    context_ = nullptr;
  }
}

void Parser::on_parser_error(const std::string& message)
{
  //Throw an exception later when the whole message has been received:
  pimpl_->parser_error_ += message;
}

void Parser::on_parser_warning(const std::string& message)
{
  //Throw an exception later when the whole message has been received:
  pimpl_->parser_warning_ += message;
}

void Parser::check_for_error_and_warning_messages()
{
  std::string msg(exception_ ? exception_->what() : "");
  bool parser_msg = false;
  bool validity_msg = false;

  if (!pimpl_->parser_error_.empty())
  {
    parser_msg = true;
    msg += "\nParser error:\n" + pimpl_->parser_error_;
    pimpl_->parser_error_.erase();
  }

  if (!pimpl_->parser_warning_.empty())
  {
    parser_msg = true;
    msg += "\nParser warning:\n" + pimpl_->parser_warning_;
    pimpl_->parser_warning_.erase();
  }

  if (parser_msg)
    exception_.reset(new parse_error(msg));
}

//static
void Parser::callback_parser_error(void* ctx, const char* msg, ...)
{
  va_list var_args;
  va_start(var_args, msg);
  callback_error_or_warning(MsgType::ParserError, ctx, msg, var_args);
  va_end(var_args);
}

//static
void Parser::callback_parser_warning(void* ctx, const char* msg, ...)
{
  va_list var_args;
  va_start(var_args, msg);
  callback_error_or_warning(MsgType::ParserWarning, ctx, msg, var_args);
  va_end(var_args);
}


//static
void Parser::callback_error_or_warning(MsgType msg_type, void* ctx,
                                       const char* msg, va_list var_args)
{
  //See xmlHTMLValidityError() in xmllint.c in libxml for more about this:

  auto context = (xmlParserCtxtPtr)ctx;
  if(context)
  {
    auto parser = static_cast<Parser*>(context->_private);
    if(parser)
    {
      auto ubuff = format_xml_error(&context->lastError);
      if (ubuff.empty())
      {
        // Usually the result of formatting var_args with the format string msg
        // is the same string as is stored in context->lastError.message.
        // It's unnecessary to use msg and var_args, if format_xml_error()
        // returns an error message (as it usually does).

        //Convert the ... to a string:
        ubuff = format_printf_message(msg, var_args);
      }

      try
      {
        switch (msg_type)
        {
          case MsgType::ParserError:
            parser->on_parser_error(ubuff);
            break;
          case MsgType::ParserWarning:
            parser->on_parser_warning(ubuff);
            break;
        }
      }
      catch (...)
      {
        parser->handle_exception();
      }
    }
  }
}

void Parser::handle_exception()
{
  try
  {
    throw; // Re-throw current exception
  }
  catch (const exception& e)
  {
    exception_.reset(e.clone());
  }
  catch (...)
  {
    exception_.reset(new wrapped_exception(std::current_exception()));
  }

  if (context_)
    xmlStopParser(context_);

  //release_underlying();
}

void Parser::check_for_exception()
{
  check_for_error_and_warning_messages();

  if (exception_)
  {
    std::unique_ptr<exception> tmp(std::move(exception_));
    tmp->raise();
  }
}

} // namespace xmlpp