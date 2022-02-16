	// OptionParser.cpp : Defines the entry point for the console application.
// Copyright (c) Thomas Rizos - Part of Rizzla Library
//
#pragma once

#include <string>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace option
{
	class Option;

	/**
	* Read contents of a file into a buffer allocated.
	* @param filename to read into memory.
	* @return returns an allocated buffer filled with the contents of the file, use option::free() to release it.
	**/
	inline char* ReadFile(const char* filename);
	/**
	* parse full commandline into main entrypoint style -> (argv, argc)
	* @param lpCmdline the command line string.
	* @param numargs returns the number of arguments in the argv.
	* @return returns an allocated argv with all arguments, use option::free() to free the returned argv memory.
	**/
	char** CommandLineToArgvA(char* lpCmdline, int* numargs);

	/**
	* free memory allocated by option functions
	* @param pPtr pointer of memory to be freed.
	**/
	void free(void* pPtr);

	enum Status
	{
		//! The argument is defined but missing value
		ARG_MISSING_VALUE = -1,

		//! The argument is not set/defined
		ARG_UNDEFINED = 0,

		//! The argument is defined.
		ARG_OK = 1,

	};

	struct Arg
	{
	public:
		enum TypeMask
		{
			Dummy = 1,
			None = 2,
			String = 4,
			Numeric = 8,
			Multiple = 16,
			Required = 32,
			End = 64
		};

		Arg() : pNext(nullptr), value(nullptr), type(Dummy), status(ARG_UNDEFINED)
		{
		}

		Arg(TypeMask type_) : pNext(nullptr), value(nullptr), type(type_), status(ARG_UNDEFINED)
		{
		}

		~Arg()
		{
			if (pNext != nullptr) delete pNext;
		}

		size_t NumberOfValues()
		{
			size_t count = 0;
			//if (m_status == ARG_OK) count++;
			Arg* pArg = this;
			while (pArg != nullptr && pArg->status == ARG_OK)
			{
				pArg = pArg->pNext;
				++count;
			}
			return count;
		}

		Arg* pNext;
		const char* value;
		TypeMask	type;
		Status		status;



	};

	struct Descriptor
	{
		const unsigned index;
		const char* const shortopt;	// set this to nullptr to terminate array of Descriptors.
		const char* const longopt;
		const int type; /* option::Arg::TypeMask */
		const char* help;
	};
	size_t desclen(const Descriptor* descs)
	{
		const Descriptor* pd = descs;
		while ((pd->type & Arg::End) != Arg::End) pd++;
		return pd - descs;
	}

	class Options
	{
	public:
		Options(const Descriptor usage[], char short_delimiter = '-', char remove_delimiter = '#')
			: _last_error_desc(-1)
			, _last_error_arg(nullptr)
			, _last_error("")
			, _option_buffer(nullptr)
			, _end_option_buffer(nullptr)
			, _buffer_pos(nullptr)
			, _short_delimiter(short_delimiter)
			, _remove_delimiter(remove_delimiter)
			, _args(nullptr)
			, _pDescriptor(&usage[0])
			, _pDescEnd(nullptr)

		{
			_numDescriptors = desclen(_pDescriptor);
			_pDescEnd = (Descriptor*)&_pDescriptor[_numDescriptors - 1];
			_args = new Arg[_numDescriptors];
			for (size_t i = 0; i < _numDescriptors; ++i)
			{
				_args->status = ARG_UNDEFINED;
				_args->type = (Arg::TypeMask)_pDescriptor[i].type;
			}
		}

		virtual ~Options()
		{
			if (_args != nullptr)			delete[] _args;
			if (_option_buffer != nullptr)	delete[] _option_buffer;

			_end_option_buffer = nullptr;
		}

		inline bool operator[](int index)
		{
			if (index >= (int)_numDescriptors || index < 0) return false;
			return _args[index].status == ARG_OK;
		}

		bool ParseFile(const char* filename)
		{
			bool success = false;
			char* cmdline = ReadFile(filename);
			if (cmdline != nullptr)
			{
				char* c = cmdline;
				do
				{
					if ((*c == '\r') || (*c == '\n'))
						*c = ' ';

				} while (*++c != 0);
				int argc = 0;
				char** argv = CommandLineToArgvA(cmdline, &argc);
				success = Parse((const char**)argv, argc);
				free(argv);
				free(cmdline);
			}

			return success;
		}

		bool Parse(const char** argv, int argc, int buffer_size = 65535)
		{
			if (argv == nullptr || argc == 0)
				return false;

			if (_option_buffer == nullptr) // second pass of arguments, append to option buffer
			{
				_option_buffer = new char[buffer_size];
				_end_option_buffer = &_option_buffer[buffer_size - 1];
				_buffer_pos = &_option_buffer[0];
			}

			bool remove_modifier = false;
			char** parg = (char**)&argv[0];
			int iArg = 0;
			const Descriptor* pTarget = nullptr;
			while (*parg != nullptr && argc--)
			{
				remove_modifier = false;
#ifdef _WIN32
				strcpy_s(_buffer_pos, (_end_option_buffer - _buffer_pos) - 1, *parg);
#else
				std::strncpy(_buffer_pos, *parg, (_end_option_buffer - _buffer_pos) - 1);
#endif
				char* arg = _buffer_pos;
				_buffer_pos += strlen(*parg);
				*_buffer_pos++ = 0;
				if ((_end_option_buffer - _buffer_pos) <= 0)
				{
					_last_error = "out of buffer while parsing command line.";
					_last_error_arg = arg;
					return false;
				}
				if (pTarget == nullptr)
				{

					char* arg_p = arg;
					size_t len = strlen(arg);
					if (len < 2)
					{
						_last_error = "missing option directive.";
						_last_error_arg = arg;
						return false;
					}

					// find assigned argument
					while (*arg_p != 0)
					{
						if (*arg_p == '=')
						{
							*arg_p = 0;
							++arg_p;
							break;
						}
						++arg_p;
					}

					if ((arg[0] == '-' && arg[1] == '-') || (arg[0] == _remove_delimiter && arg[1] == _remove_delimiter))
						pTarget = FindLong(&arg[2]);
					else if (arg[0] == _short_delimiter || arg[0] == _remove_delimiter)
						pTarget = FindShort(&arg[1]);

					if (arg[0] == _remove_delimiter)
						remove_modifier = true;

					if (pTarget == desc_end() || pTarget == nullptr)
					{
						pTarget = nullptr;
						_last_error = "invalid option directive.";
						_last_error_arg = arg;
						return false;
					}

					if ((pTarget->type & Arg::None) == Arg::None)
					{
						if (remove_modifier)
						{
							_args[pTarget->index].status = ARG_UNDEFINED;
						}
						else
						{
							_args[pTarget->index].status = ARG_OK;
						}
						pTarget = nullptr;
					}
					else if (*arg_p != 0)
					{
						if (remove_modifier)
						{
							Rem(pTarget);
						}
						else
						{
							Add(pTarget, arg_p);
							pTarget = nullptr;
						}
					}
				}
				else
				{
					Add(pTarget, arg);
					pTarget = nullptr;
				}

				parg++;
				iArg++;
			}
			if (pTarget != nullptr)
			{
				_args[pTarget->index].status = ARG_MISSING_VALUE;
			}

			return validate();
		}

		Arg* GetArgument(int index)
		{
			if (index >= (int)_numDescriptors || index < 0) return nullptr;
			return &_args[index];
		}

		Arg* GetLastArgument(int index)
		{
			if (index >= (int)_numDescriptors || index < 0) return nullptr;
			Arg* a = &_args[index];
			while (a->pNext != nullptr)	a = a->pNext;
			return a;
		}

		bool GetArgument(int index, const char** retval, const char* defaultValue)
		{
			*retval = defaultValue;
			Arg* arg = GetLastArgument(index);
			if (arg == nullptr) return false;
			*retval = arg->value;
			return true;
		}

		bool GetArgument(int index, const char** retval)
		{
			Arg* arg = GetLastArgument(index);
			if (arg == nullptr) return false;
			*retval = arg->value;
			return true;
		}

		bool GetArgument(int index, int& retval)
		{
			Arg* arg = GetLastArgument(index);
			if (arg == nullptr) return false;
			retval = atoi(arg->value);
			return true;
		}

		bool GetArgument(int index, long& retval)
		{
			Arg* arg = GetLastArgument(index);
			if (arg == nullptr) return false;
			retval = atol(arg->value);
			return true;
		}

		const char* GetValue(int index)
		{
			Arg* arg = GetLastArgument(index);
			if (arg == nullptr) return nullptr;
			return arg->value;
		}

		char* error_msg()
		{
			if (_last_error_arg != nullptr)
				snprintf(_message, MessageBufferLenght, "%s: %s", _last_error_arg, _last_error);
			else if (_last_error_desc > 0)
				snprintf(_message, MessageBufferLenght, "--%s or %c%s: %s", _pDescriptor[_last_error_desc].longopt, _short_delimiter, _pDescriptor[_last_error_desc].shortopt, _last_error);
			return _message;
		}

		void print()
		{
			printf("Usage: \n");
			for (size_t i = 0; i < _numDescriptors; ++i)
			{
				const Descriptor& desc = _pDescriptor[i];
				if ((desc.type & Arg::Dummy) != Arg::Dummy)
					printf("\t%s %s\n", (desc.type & Arg::Required) == Arg::Required ? "[req]" : "[opt]", desc.help);
				else
					printf("\t%s\n", desc.help);
			}
		}

		const char* cstr()
		{
			if (_help_buffer.empty())
			{
				std::stringstream ss;
				ss << "Usage: \n";
				for (size_t i = 0; i < _numDescriptors; ++i)
				{
					const Descriptor& desc = _pDescriptor[i];
					if ((desc.type & Arg::Dummy) != Arg::Dummy)
						ss << "\t" << ((desc.type & Arg::Required) == Arg::Required ? "[req]" : "[opt]") << desc.help << "\n";
					else
						ss << "\t" << desc.help << "\n";
				}
				_help_buffer = ss.str();
			}
			return _help_buffer.c_str();
		}

	private:

		const Descriptor* FindShort(char* key)
		{
			for (size_t i = 0; i < _numDescriptors; ++i)
			{
				if (_pDescriptor[i].shortopt != nullptr && strcmp(_pDescriptor[i].shortopt, key) == 0)
					return &_pDescriptor[i];
			}
			return desc_end();
		}

		const Descriptor* FindLong(char* key)
		{
			for (size_t i = 0; i < _numDescriptors; ++i)
			{
				if (_pDescriptor[i].longopt != nullptr && strcmp(_pDescriptor[i].longopt, key) == 0)
					return &_pDescriptor[i];
			}
			return desc_end();
		}

		void Add(const Descriptor* key, char* value)
		{
			Add(&_args[key->index], value);
		}

		void Add(Arg* arg, const char* value)
		{
			if (arg->status == ARG_UNDEFINED)
			{
				arg->status = ARG_OK;
				arg->value = value;
			}
			else
			{
				if (arg->pNext != nullptr)
				{
					Add(arg->pNext, value);
				}
				else
				{
					arg->pNext = new Arg(arg->type);
					Add(arg->pNext, value);
				}
			}
		}
		void Rem(const Descriptor* key)
		{
			Rem(&_args[key->index]);
		}

		void Rem(Arg* arg)
		{
			if (arg->status == ARG_UNDEFINED)
			{

			}
			else
			{
				arg->pNext = new Arg(arg->type);
				Add(arg->pNext, "");
				arg->pNext->status = ARG_UNDEFINED;
			}
		}
		inline const Descriptor* desc_begin() { return _pDescriptor; }

		inline const Descriptor* desc_end() { return _pDescEnd; }


	private:
		bool validate()
		{
			for (size_t i = 0; i < _numDescriptors; ++i)
			{
				const Descriptor& desc = _pDescriptor[i];
				Arg& src = _args[desc.index];

				if (src.status != ARG_OK && (desc.type & Arg::Required) == Arg::Required)
				{
					_last_error = "required argument missing.";
					_last_error_desc = (int)i;
					return false;
				}

				if (src.status == ARG_OK && (desc.type & Arg::Numeric) == Arg::Numeric)
				{
					size_t len = src.value == nullptr ? 0 : strlen(src.value);
					unsigned int allnumeric = 1;
					for (unsigned int l = 0; l < len && allnumeric; ++l)
					{
						allnumeric &= (int)(isdigit(src.value[l]) != 0 || src.value[l] == '-');
					}

					if (src.value == nullptr)
					{
						_last_error = "missing numeric value.";
						_last_error_desc = (int)i;
					}
					if (!allnumeric)
					{
						_last_error = "not a numeric value.";
						_last_error_desc = (int)i;
						return false;
					}
				}

				if (src.status == ARG_OK && (desc.type & Arg::Multiple) != Arg::Multiple && src.NumberOfValues() > 1)
				{
					_last_error = "more than one argument of same name.";
					_last_error_desc = (int)i;
					return false;
				}
			}
			return true;
		}

		const int			MessageBufferLenght = 1024;
		char				_message[1024];
		int					_last_error_desc;
		char* _last_error_arg;
		const char* _last_error;
		char* _option_buffer;
		char* _end_option_buffer;
		char* _buffer_pos;
		char				_short_delimiter;
		char				_remove_delimiter;
		Arg* _args;
		const Descriptor* _pDescriptor;
		Descriptor* _pDescEnd;
		size_t				_numDescriptors;
		std::string			_help_buffer;


	};

	inline char* ReadFile(const char* filename)
	{
		FILE* f = nullptr;
		fopen_s(&f, filename, "r");
		if (f == 0)
			return nullptr;
		fseek(f, 0, SEEK_END);
		size_t size = ftell(f);
		fseek(f, 0, SEEK_SET);
		char* buffer = (char*)malloc((sizeof(char) * (size)) + 1);
		fread(buffer, 1, size, f);
		fclose(f);

		return buffer;
	}


	void free(void* argv)
	{
		::free(argv);
	}

	/*************************************************************************
 * CommandLineToArgvA            [SHELL32.@] (Wine)
 *
 * We must interpret the quotes in the command line to rebuild the argv
 * array correctly:
 * - arguments are separated by spaces or tabs
 * - quotes serve as optional argument delimiters
 *   '"a b"'   -> 'a b'
 * - escaped quotes must be converted back to '"'
 *   '\"'      -> '"'
 * - consecutive backslashes preceding a quote see their number halved with
 *   the remainder escaping the quote:
 *   2n   backslashes + quote -> n backslashes + quote as an argument delimiter
 *   2n+1 backslashes + quote -> n backslashes + literal quote
 * - backslashes that are not followed by a quote are copied literally:
 *   'a\b'     -> 'a\b'
 *   'a\\b'    -> 'a\\b'
 * - in quoted strings, consecutive quotes see their number divided by three
 *   with the remainder modulo 3 deciding whether to close the string or not.
 *   Note that the opening quote must be counted in the consecutive quotes,
 *   that's the (1+) below:
 *   (1+) 3n   quotes -> n quotes
 *   (1+) 3n+1 quotes -> n quotes plus closes the quoted string
 *   (1+) 3n+2 quotes -> n+1 quotes plus closes the quoted string
 * - in unquoted strings, the first quote opens the quoted string and the
 *   remaining consecutive quotes follow the above rule.
 *
 */
	char** CommandLineToArgvA(char* lpCmdline, int* numargs)
	{
		uint32_t argc;
		char** argv;
		char* s;
		char* d;
		char* cmdline;
		int qcount, bcount;

		if (!numargs || *lpCmdline == 0)
		{
			return NULL;
		}

		/* --- First count the arguments */
		argc = 1;
		s = lpCmdline;
		/* The first argument, the executable path, follows special rules */
		if (*s == '"')
		{
			/* The executable path ends at the next quote, no matter what */
			s++;
			while (*s)
				if (*s++ == '"')
					break;
		}
		else
		{
			/* The executable path ends at the next space, no matter what */
			while (*s && *s != ' ' && *s != '\t')
				s++;
		}
		/* skip to the first argument, if any */
		while (*s == ' ' || *s == '\t')
			s++;
		if (*s)
			argc++;

		/* Analyze the remaining arguments */
		qcount = bcount = 0;
		while (*s)
		{
			if ((*s == ' ' || *s == '\t') && qcount == 0)
			{
				/* skip to the next argument and count it if any */
				while (*s == ' ' || *s == '\t')
					s++;
				if (*s)
					argc++;
				bcount = 0;
			}
			else if (*s == '\\')
			{
				/* '\', count them */
				bcount++;
				s++;
			}
			else if (*s == '"')
			{
				/* '"' */
				if ((bcount & 1) == 0)
					qcount++; /* unescaped '"' */
				s++;
				bcount = 0;
				/* consecutive quotes, see comment in copying code below */
				while (*s == '"')
				{
					qcount++;
					s++;
				}
				qcount = qcount % 3;
				if (qcount == 2)
					qcount = 0;
			}
			else
			{
				/* a regular character */
				bcount = 0;
				s++;
			}
		}

		/* Allocate in a single lump, the string array, and the strings that go
		 * with it. This way the caller can make a single LocalFree() call to free
		 * both, as per MSDN.
		 */
		size_t len = strlen(lpCmdline) + 1;
		argv = (char**)malloc((argc + 1) * sizeof(char*) + (strlen(lpCmdline) + 1) * sizeof(char));
		if (!argv)
			return nullptr;
		cmdline = (char*)(argv + argc + 1);
		strcpy_s(cmdline, len, lpCmdline);

		/* --- Then split and copy the arguments */
		argv[0] = d = cmdline;
		argc = 1;
		/* The first argument, the executable path, follows special rules */
		if (*d == '"')
		{
			/* The executable path ends at the next quote, no matter what */
			s = d + 1;
			while (*s)
			{
				if (*s == '"')
				{
					s++;
					break;
				}
				*d++ = *s++;
			}
		}
		else
		{
			/* The executable path ends at the next space, no matter what */
			while (*d && *d != ' ' && *d != '\t')
				d++;
			s = d;
			if (*s)
				s++;
		}
		/* close the executable path */
		*d++ = 0;
		/* skip to the first argument and initialize it if any */
		while (*s == ' ' || *s == '\t')
			s++;
		if (!*s)
		{
			/* There are no parameters so we are all done */
			argv[argc] = nullptr;
			*numargs = argc;
			return argv;
		}

		/* Split and copy the remaining arguments */
		argv[argc++] = d;
		qcount = bcount = 0;
		while (*s)
		{
			if ((*s == ' ' || *s == '\t') && qcount == 0)
			{
				/* close the argument */
				*d++ = 0;
				bcount = 0;

				/* skip to the next one and initialize it if any */
				do {
					s++;
				} while (*s == ' ' || *s == '\t');
				if (*s)
					argv[argc++] = d;
			}
			else if (*s == '\\')
			{
				*d++ = *s++;
				bcount++;
			}
			else if (*s == '"')
			{
				if ((bcount & 1) == 0)
				{
					/* Preceded by an even number of '\', this is half that
					 * number of '\', plus a quote which we erase.
					 */
					d -= bcount / 2;
					qcount++;
				}
				else
				{
					/* Preceded by an odd number of '\', this is half that
					 * number of '\' followed by a '"'
					 */
					d = d - bcount / 2 - 1;
					*d++ = '"';
				}
				s++;
				bcount = 0;
				/* Now count the number of consecutive quotes. Note that qcount
				 * already takes into account the opening quote if any, as well as
				 * the quote that lead us here.
				 */
				while (*s == '"')
				{
					if (++qcount == 3)
					{
						*d++ = '"';
						qcount = 0;
					}
					s++;
				}
				if (qcount == 2)
					qcount = 0;
			}
			else
			{
				/* a regular character */
				*d++ = *s++;
				bcount = 0;
			}
		}
		*d = '\0';
		argv[argc] = NULL;
		*numargs = argc;

		return argv;
	}


#ifdef _WIN32

	char** CommandLineToArgvWin(LPSTR lpCmdline, int* numargs)
	{
		lpCmdline = ::GetCommandLineA();
		return CommandLineToArgvA(lpCmdline, numargs);
	}

#endif

}