#include "PrecompiledHeader.hpp"
#include "LibraryInterface.hpp"
#include "Application\Application.hpp"
#include <SDR Library API\ExportTypes.hpp>

#include "Interface\Application\Modules\Shared\Console.hpp"

extern "C"
{
	#include <libavcodec\avcodec.h>
	#include <libavformat\avformat.h>
}

namespace
{
	namespace ModuleGameDir
	{
		std::string ResourcePath;
		std::string GamePath;
	}
}

namespace
{
	enum
	{
		LibraryVersion = 27,
	};
}

namespace
{
	namespace Commands
	{
		void Version()
		{
			SDR::Log::Message("SDR: Library version: %d\n", LibraryVersion);
		}
	}

	void RegisterLAV()
	{
		avcodec_register_all();
		av_register_all();
	}

	/*
		Creation has to be delayed as the necessary console stuff isn't available earlier.
	*/
	SDR::StartupFunctionAdder A1("LibraryInterface console commands", []()
	{
		SDR::Console::MakeCommand("sdr_version", Commands::Version);
	});

	struct LoadFuncData : SDR::API::ShadowState
	{
		void Create(SDR::API::StageType stage)
		{
			auto pipename = SDR::API::CreatePipeName(stage);
			auto successname = SDR::API::CreateEventSuccessName(stage);
			auto failname = SDR::API::CreateEventFailureName(stage);

			Pipe.Attach(CreateFileA(pipename.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
			EventSuccess.Attach(OpenEventA(EVENT_MODIFY_STATE, false, successname.c_str()));
			EventFailure.Attach(OpenEventA(EVENT_MODIFY_STATE, false, failname.c_str()));
		}

		~LoadFuncData()
		{
			if (Failure)
			{
				SetEvent(EventFailure.Get());
			}

			else
			{
				SetEvent(EventSuccess.Get());
			}
		}

		void Write(const std::string& text)
		{
			DWORD written;
			WriteFile(Pipe.Get(), text.c_str(), text.size(), &written, nullptr);
		}

		bool Failure = false;
	};

	auto CreateShadowLoadState(SDR::API::StageType stage)
	{
		static LoadFuncData* LoadDataPtr;

		auto localdata = std::make_unique<LoadFuncData>();
		localdata->Create(stage);

		LoadDataPtr = localdata.get();

		/*
			Temporary communication gates. All text output has to go to the launcher console.
		*/
		SDR::Log::SetMessageFunction([](std::string&& text)
		{
			LoadDataPtr->Write(text);
		});

		SDR::Log::SetMessageColorFunction([](SDR::Shared::Color col, std::string&& text)
		{
			LoadDataPtr->Write(text);
		});

		SDR::Log::SetWarningFunction([](std::string&& text)
		{
			LoadDataPtr->Write(text);
		});

		return localdata;
	}
}

void SDR::Library::Load()
{
	auto localdata = CreateShadowLoadState(SDR::API::StageType::Load);

	try
	{
		SDR::Setup();
		SDR::Log::Message("SDR: Source Demo Render loaded\n");

		/*
			Give all output to the game console now.
		*/
		Console::Load();
	}

	catch (const SDR::Error::Exception& error)
	{
		localdata->Failure = true;
	}
}

void SDR::Library::Unload()
{
	SDR::Close();
}

const char* SDR::Library::GetGamePath()
{
	return ModuleGameDir::GamePath.c_str();
}

const char* SDR::Library::GetResourcePath()
{
	return ModuleGameDir::ResourcePath.c_str();
}

std::string SDR::Library::BuildResourcePath(const char* file)
{
	std::string ret = GetResourcePath();
	ret += file;

	return ret;
}

extern "C"
{
	__declspec(dllexport) int __cdecl SDR_LibraryVersion()
	{
		return LibraryVersion;
	}

	/*
		First actual pre-engine load function. Don't reference any
		engine libraries here as they aren't loaded yet like in "Load".
	*/
	__declspec(dllexport) void __cdecl SDR_Initialize(const char* respath, const char* gamepath)
	{
		ModuleGameDir::ResourcePath = respath;
		ModuleGameDir::GamePath = gamepath;

		SDR::Error::SetPrintFormat("SDR: %s\n");

		auto localdata = CreateShadowLoadState(SDR::API::StageType::Initialize);

		try
		{
			SDR::PreEngineSetup();
		}

		catch (const SDR::Error::Exception& error)
		{
			localdata->Failure = true;
			return;
		}

		RegisterLAV();
	}
}
