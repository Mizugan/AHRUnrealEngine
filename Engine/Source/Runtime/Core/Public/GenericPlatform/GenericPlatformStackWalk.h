// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
	GenericPlatformStackWalk.h: Generic platform stack walking functions...do not do anything
==============================================================================================*/

#pragma once

/**
* This is used to capture all of the module information needed to load pdb's.
* @todo, the memory profiler should not be using this as platform agnostic
*/
struct FStackWalkModuleInfo
{
	uint64 BaseOfImage;
	uint32 ImageSize;
	uint32 TimeDateStamp;
	TCHAR ModuleName[32];
	TCHAR ImageName[256];
	TCHAR LoadedImageName[256];
	uint32 PdbSig;
	uint32 PdbAge;
	struct
	{
		unsigned long  Data1;
		unsigned short Data2;
		unsigned short Data3;
		unsigned char  Data4[8];
	} PdbSig70;
};

/**
 * Symbol information associated with a program counter.
 */
struct FProgramCounterSymbolInfo /*final*/
{
	enum
	{
		/** Length of the string used to store the symbol's names, including the trailing character. */
		MAX_NAME_LENGHT = 1024,
	};

	/** Module name. */
	ANSICHAR	ModuleName[MAX_NAME_LENGHT];

	/** Function name. */
	ANSICHAR	FunctionName[MAX_NAME_LENGHT];

	/** Filename. */
	ANSICHAR	Filename[MAX_NAME_LENGHT];

	/** Line number in file. */
	int32		LineNumber;

	/** Symbol displacement of address.	*/
	int32		SymbolDisplacement;

	/** Program counter offset into module. */
	uint64		OffsetInModule;

	/** Program counter. */
	uint64		ProgramCounter;

	/** Default constructor. */
	FProgramCounterSymbolInfo();
};

/**
* Generic implementation for most platforms
**/
struct CORE_API FGenericPlatformStackWalk
{
	typedef FGenericPlatformStackWalk Base;

	/**
	 * Initializes stack traversal and symbol. Must be called before any other stack/symbol functions. Safe to reenter.
	 */
	static bool InitStackWalking()
	{
		return 1;
	}

	/**
	 * Converts the passed in program counter address to a human readable string and appends it to the passed in one.
	 * @warning: The code assumes that HumanReadableString is large enough to contain the information.
	 * @warning: Please, do not override this method. Can't be modified or altered without notice.
	 * 
	 * This method is the same for all platforms to simplify parsing by the crash processor.
	 * 
	 * Example formatted line:
	 *
	 * UE4Editor_Core!FOutputDeviceWindowsError::Serialize() (0xddf1bae5) + 620 bytes [\engine\source\runtime\core\private\windows\windowsplatformoutputdevices.cpp:110]
	 * ModuleName!FunctionName (ProgramCounter) + offset bytes [StrippedFilepath:LineNumber]
	 *
	 * @param	CurrentCallDepth		Depth of the call, if known (-1 if not - note that some platforms may not return meaningful information in the latter case)
	 * @param	ProgramCounter			Address to look symbol information up for
	 * @param	HumanReadableString		String to concatenate information with
	 * @param	HumanReadableStringSize size of string in characters
	 * @param	Context					Pointer to crash context, if any
	 * @return	true if the symbol was found, otherwise false
	 */ 
	static bool ProgramCounterToHumanReadableString( int32 CurrentCallDepth, uint64 ProgramCounter, ANSICHAR* HumanReadableString, SIZE_T HumanReadableStringSize, FGenericCrashContext* Context = NULL );

	/**
	 * Converts the passed in program counter address to a symbol info struct, filling in module and filename, line number and displacement.
	 * @warning: The code assumes that the destination strings are big enough
	 *
	 * @param	ProgramCounter		Address to look symbol information up for
	 * @param	out_SymbolInfo		Symbol information associated with program counter
	 */
	static void ProgramCounterToSymbolInfo( uint64 ProgramCounter, FProgramCounterSymbolInfo& out_SymbolInfo)
	{
		out_SymbolInfo.ProgramCounter = ProgramCounter;
	}

	/**
	 * Capture a stack backtrace and optionally use the passed in exception pointers.
	 *
	 * @param	BackTrace			[out] Pointer to array to take backtrace
	 * @param	MaxDepth			Entries in BackTrace array
	 * @param	Context				Optional thread context information
	 */
	static void CaptureStackBackTrace( uint64* BackTrace, uint32 MaxDepth, void* Context = NULL );

	/**
	 * Walks the stack and appends the human readable string to the passed in one.
	 * @warning: The code assumes that HumanReadableString is large enough to contain the information.
	 *
	 * @param	HumanReadableString	String to concatenate information with
	 * @param	HumanReadableStringSize size of string in characters
	 * @param	IgnoreCount			Number of stack entries to ignore (some are guaranteed to be in the stack walking code)
	 * @param	Context				Optional thread context information
	 */ 
	static void StackWalkAndDump( ANSICHAR* HumanReadableString, SIZE_T HumanReadableStringSize, int32 IgnoreCount, void* Context = NULL );

	/**
	 * Returns the number of modules loaded by the currently running process.
	 */
	FORCEINLINE static int32 GetProcessModuleCount()
	{
		return 0;
	}

	/**
	 * Gets the signature for every module loaded by the currently running process.
	 *
	 * @param	ModuleSignatures		An array to retrieve the module signatures.
	 * @param	ModuleSignaturesSize	The size of the array pointed to by ModuleSignatures.
	 *
	 * @return	The number of modules copied into ModuleSignatures
	 */
	FORCEINLINE int32 GetProcessModuleSignatures(FStackWalkModuleInfo *ModuleSignatures, const int32 ModuleSignaturesSize)
	{
		return 0;
	}
};
