#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>

#include "tr_local.h"
#include "tr_allocator.h"

namespace
{

using StringList = std::vector<std::string>;

bool ShouldEscape( char c )
{
	switch ( c )
	{
	case '\\':
	case '"':
		return true;
	default:
		return false;
	}
}

std::string& Escape( std::string& s )
{
	std::string::difference_type escapableCharacters = std::count_if( s.begin(), s.end(), ShouldEscape );
	if ( escapableCharacters == 0 )
	{
		return s;
	}

	if ( s.capacity() < (s.length() + escapableCharacters) )
	{
		// Grow if necessary.
		s.resize(s.length() + escapableCharacters);
	}

	std::string::iterator it = s.begin();
	while ( it != s.end() )
	{
		char c = *it;
		if ( ShouldEscape(c) )
		{
			it = s.insert(it, '\\');
			it += 2;
		}
		else
		{
			++it;
		}
	}

	return s;
}

bool EndsWith( const std::string& s, const std::string& suffix )
{
	return s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0;
}

const char *GetShaderSuffix( GPUShaderType type )
{
	switch ( type )
	{
		case GPUSHADER_VERTEX:   return "_vp";
		case GPUSHADER_FRAGMENT: return "_fp";
		case GPUSHADER_GEOMETRY: return "_gp";
		case GPUSHADER_COMPUTE:  return "_cp";
		default: assert(!"Invalid shader type");
	}
	return nullptr;
}

const char *ToString( GPUShaderType type )
{
	switch ( type )
	{
		case GPUSHADER_VERTEX:   return "GPUSHADER_VERTEX";
		case GPUSHADER_FRAGMENT: return "GPUSHADER_FRAGMENT";
		case GPUSHADER_GEOMETRY: return "GPUSHADER_GEOMETRY";
		case GPUSHADER_COMPUTE:  return "GPUSHADER_COMPUTE";
		default: assert(!"Invalid shader type");
	}
	return nullptr;
}

} // anonymous namespace

int main( int argc, char *argv[] )
{
	StringList args(argv, argv + argc);

	if ( args.empty() )
	{
		std::cerr << "No GLSL files were given.\n";
		return EXIT_FAILURE;
	}

	if ( args.size() < 4 )
	{
		// 0 = exe, 1 = cpp file, 2 = h file, 2+ = glsl files
		return EXIT_FAILURE;
	}

	std::string& shadersCppFile = args[1];
	std::string& shadersHeaderFile = args[2];
	StringList glslFiles(args.begin() + 3, args.end());

	std::cout << "Outputting to '" << shadersCppFile << "' and '" << shadersHeaderFile << "'\n";

	Allocator allocator(512 * 1024);

	std::ostringstream cppStream;
	std::ostringstream headerStream;
	std::string line;

	headerStream << "// This file is auto-generated. DO NOT EDIT BY HAND\n";
	headerStream << "#pragma once\n\n";
	headerStream << "#include \"tr_local.h\"\n\n";

	cppStream << "// This file is auto-generated. DO NOT EDIT BY HAND\n";
	cppStream << "#include \"tr_local.h\"\n\n";
	// joins the chunks of a long shader (see below), kept for the lifetime of
	// the module like the literals
	cppStream <<
		"static const char *JoinGLSLChunks( const char *const *chunks )\n"
		"{\n"
		"\tsize_t size = 1;\n"
		"\tfor ( const char *const *c = chunks; *c; ++c )\n"
		"\t\tsize += strlen(*c);\n"
		"\tchar *source = (char *)malloc(size);\n"
		"\tsource[0] = '\\0';\n"
		"\tfor ( const char *const *c = chunks; *c; ++c )\n"
		"\t\tstrcat(source, *c);\n"
		"\treturn source;\n"
		"}\n\n";
	for ( StringList::const_iterator it = glslFiles.begin();
			it != glslFiles.end(); ++it )
	{
		// Get shader name from file name
		if ( !EndsWith(*it, ".glsl") )
		{
			std::cerr << *it << " doesn't end with .glsl extension.\n";
			continue;
		}

		std::string::size_type lastSlash = it->find_last_of("\\/");
		std::string shaderName(it->begin() + lastSlash + 1, it->end() - 5);

		// Write, one line at a time to the output
		std::ifstream fs(it->c_str());
		if ( !fs )
		{
			std::cerr << *it << " could not be opened.\n";
			continue;
		}

		//from: https://stackoverflow.com/questions/22984956/tellg-function-give-wrong-size-of-file/22986486
		fs.ignore(std::numeric_limits<std::streamsize>::max());
		std::streamsize fileSize = fs.gcount();
		fs.clear();   //  Since ignore will have set eof.
		fs.seekg(0, std::ios::beg);

		allocator.Reset();

		char *programText = ojkAllocString(allocator, fileSize);
		memset(programText, 0, (size_t)fileSize + 1);
		fs.read(programText, fileSize);

		GPUProgramDesc programDesc = ParseProgramSource(allocator,  programText);
		for ( size_t i = 0, numShaders = programDesc.numShaders; i < numShaders; ++i )
		{
			GPUShaderDesc& shaderDesc = programDesc.shaders[i];
			const char *suffix = GetShaderSuffix(shaderDesc.type);

			// MSVC limits a string literal to 64 KB (C1091): long shaders are
			// written as an array of chunks, joined once at start up
			const std::string variable = "fallback_" + shaderName + suffix;
			const bool chunked = strlen(shaderDesc.source) > 16000;
			size_t chunkSize = 0;
			if ( chunked )
				cppStream << "static const char *const " << variable << "_chunks[] = {\n\"";
			else
				cppStream << "const char *" << variable << " = \"";

			const char *lineStart = shaderDesc.source;
			const char *lineEnd = strchr(lineStart, '\n');
			while ( lineEnd )
			{
				line.assign(lineStart, lineEnd - lineStart);
				chunkSize += line.length() + 1;
				cppStream << Escape(line);
				cppStream << "\\n\"\n\"";

				lineStart = lineEnd + 1;
				lineEnd = strchr(lineStart, '\n');
				if ( chunked && chunkSize > 15000 && lineEnd )
				{
					// a new array element, not a continued literal
					cppStream << "\",\n\"";
					chunkSize = 0;
				}
			}

			line.assign(lineStart);
			if ( chunked )
			{
				cppStream << Escape(line) << "\",\n  nullptr\n};\n";
				cppStream << "const char *" << variable << " = JoinGLSLChunks(" << variable << "_chunks);\n";
			}
			else
			{
				cppStream << Escape(line) << "\";\n";
			}
		}

		cppStream << "GPUShaderDesc fallback_" << shaderName << "Shaders[] = {\n";
		for ( size_t i = 0, numShaders = programDesc.numShaders; i < numShaders; ++i )
		{
			GPUShaderDesc& shaderDesc = programDesc.shaders[i];
			const char *suffix = GetShaderSuffix(shaderDesc.type);

			cppStream << "  { " << ToString(shaderDesc.type) << ", "
						"fallback_" << shaderName << suffix << ", "
						<< shaderDesc.firstLineNumber << " },\n";
		}
		cppStream << "};\n";

		cppStream << "extern const GPUProgramDesc fallback_" << shaderName << "Program = { "
			<< programDesc.numShaders << ", fallback_" << shaderName << "Shaders };\n\n";

		headerStream << "extern const GPUProgramDesc fallback_" << shaderName << "Program;\n";
	}

	std::ofstream cppFile(shadersCppFile);
	if ( !cppFile )
	{
		std::cerr << "Could not create file '" << shadersCppFile << "'\n";
	}
	else
	{
		cppFile << cppStream.str();
	}

	std::ofstream headerFile(shadersHeaderFile);
	if (!headerFile)
	{
		std::cerr << "Could not create file '" << shadersHeaderFile << "'\n";
	}
	else
	{
		headerFile << headerStream.str();
	}
}
