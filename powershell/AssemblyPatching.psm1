#!/usr/bin/env pwsh
#Requires -Version 5.1
Set-StrictMode -Version Latest

<#
.SYNOPSIS
    Shared IL patching utilities for CameraUnlock mods using Mono.Cecil.
.DESCRIPTION
    Provides common assembly patching operations:
    - Screen center raycast patching (for aim decoupling)
    - Method injection
    - Patch marker management
    - Safe assembly modification with backup/restore

    IMPORTANT: Mono.Cecil must be loaded before using these functions.
    Call Initialize-AssemblyPatching first with the path to Mono.Cecil.dll.
#>

# Track if Mono.Cecil is loaded
$Script:CecilLoaded = $false
$Script:CecilPath = $null

# Compiled patcher types, keyed by patch marker. The marker is baked into the
# generated type as a const, so one cached type per marker: a session that
# patches two assemblies with different markers (a mod moving _v2 to _v3, a
# batch dev-deploy across mods) must not reuse the first marker's type.
$Script:PatcherTypes = @{}

<#
.SYNOPSIS
    Initializes the assembly patching module by loading Mono.Cecil.
.PARAMETER CecilPath
    Path to Mono.Cecil.dll.
#>
function Initialize-AssemblyPatching {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$CecilPath
    )

    if (-not (Test-Path -LiteralPath $CecilPath)) {
        throw "Mono.Cecil.dll not found at: $CecilPath"
    }

    Add-Type -LiteralPath $CecilPath
    $Script:CecilLoaded = $true
    $Script:CecilPath = $CecilPath
    Write-Host "Loaded Mono.Cecil from: $CecilPath" -ForegroundColor Gray
}

<#
.SYNOPSIS
    Gets the inline C# patcher code for screen center raycast patching.
.DESCRIPTION
    Returns the C# code that can be compiled and executed to patch assemblies.
    This code patches `new Vector3(Screen.width/2, Screen.height/2, 0)` patterns
    to call a custom method instead.
.PARAMETER PatchMarker
    Name of the marker type to add (default: "HeadTracking_Patched_v2").
.PARAMETER TypeName
    Name of the generated class (default: "ScreenCenterPatcher"). The marker is
    a const in the generated type, so a caller compiling more than one marker in
    a single session needs a distinct name per marker.
.OUTPUTS
    String containing the C# patcher code.
#>
function Get-ScreenCenterPatcherCode {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$false)]
        [string]$PatchMarker = "HeadTracking_Patched_v2",

        [Parameter(Mandatory=$false)]
        [string]$TypeName = "ScreenCenterPatcher"
    )

    return @"
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Mono.Cecil;
using Mono.Cecil.Cil;

public static class $TypeName
{
    private const string PatchMarker = "$PatchMarker";

    /// <summary>
    /// Checks if an assembly is already patched.
    /// </summary>
    public static bool IsPatched(AssemblyDefinition assembly)
    {
        return assembly.MainModule.Types.Any(t => t.Name == PatchMarker);
    }

    /// <summary>
    /// Adds a patch marker type to the assembly.
    /// </summary>
    public static void AddPatchMarker(AssemblyDefinition assembly)
    {
        var markerType = new TypeDefinition(
            "HeadTracking",
            PatchMarker,
            TypeAttributes.NotPublic | TypeAttributes.Class,
            assembly.MainModule.TypeSystem.Object);
        assembly.MainModule.Types.Add(markerType);
    }

    /// <summary>
    /// Injects a static method call at the end of a method (before ret).
    /// </summary>
    public static bool InjectMethodCall(MethodDefinition targetMethod, MethodReference methodToCall)
    {
        if (targetMethod == null || !targetMethod.HasBody)
            return false;

        var il = targetMethod.Body.GetILProcessor();
        var retInstruction = targetMethod.Body.Instructions.Last();

        if (retInstruction.OpCode != OpCodes.Ret)
        {
            // Find the last ret instruction
            retInstruction = targetMethod.Body.Instructions.LastOrDefault(i => i.OpCode == OpCodes.Ret);
            if (retInstruction == null)
                return false;
        }

        var callInstruction = il.Create(OpCodes.Call, methodToCall);
        il.InsertBefore(retInstruction, callInstruction);

        // Redirect all branches that targeted ret to target our call instead,
        // so the injected call runs on ALL exit paths (not just fall-through).
        foreach (var instr in targetMethod.Body.Instructions)
        {
            if (instr.Operand == retInstruction)
            {
                instr.Operand = callInstruction;
            }

            var switchTargets = instr.Operand as Instruction[];
            if (switchTargets != null)
            {
                for (int t = 0; t < switchTargets.Length; t++)
                {
                    if (switchTargets[t] == retInstruction)
                        switchTargets[t] = callInstruction;
                }
            }
        }

        // TryEnd/HandlerEnd are exclusive bounds. One pointing at ret would
        // swallow the instruction we just inserted before it, putting the
        // injected call inside a protected region it was never in. Pull the
        // bound back to the call so the region keeps its original extent.
        foreach (var handler in targetMethod.Body.ExceptionHandlers)
        {
            if (handler.TryEnd == retInstruction) handler.TryEnd = callInstruction;
            if (handler.HandlerEnd == retInstruction) handler.HandlerEnd = callInstruction;
        }

        return true;
    }

    /// <summary>
    /// Repoints every reference to an instruction that is about to be removed
    /// at its replacement: branch/leave operands, switch target arrays, and
    /// exception-handler bounds. A dangling reference makes AssemblyDefinition.Write
    /// emit a branch to offset 0 (or throw), producing a method that fails to JIT.
    /// </summary>
    private static void RetargetReferences(MethodBody body, List<Instruction> removed, Instruction replacement)
    {
        foreach (var instr in body.Instructions)
        {
            var target = instr.Operand as Instruction;
            if (target != null && removed.Contains(target))
            {
                instr.Operand = replacement;
                continue;
            }

            var switchTargets = instr.Operand as Instruction[];
            if (switchTargets != null)
            {
                for (int t = 0; t < switchTargets.Length; t++)
                {
                    if (removed.Contains(switchTargets[t]))
                        switchTargets[t] = replacement;
                }
            }
        }

        foreach (var handler in body.ExceptionHandlers)
        {
            if (handler.TryStart != null && removed.Contains(handler.TryStart)) handler.TryStart = replacement;
            if (handler.TryEnd != null && removed.Contains(handler.TryEnd)) handler.TryEnd = replacement;
            if (handler.HandlerStart != null && removed.Contains(handler.HandlerStart)) handler.HandlerStart = replacement;
            if (handler.HandlerEnd != null && removed.Contains(handler.HandlerEnd)) handler.HandlerEnd = replacement;
            if (handler.FilterStart != null && removed.Contains(handler.FilterStart)) handler.FilterStart = replacement;
        }
    }

    /// <summary>
    /// Patches screen center raycast patterns in a method.
    /// Replaces: new Vector3(Screen.width / 2, Screen.height / 2, 0f)
    /// With: call to the specified replacement method
    /// </summary>
    public static int PatchScreenCenterRaycasts(MethodDefinition method, MethodReference replacementMethod)
    {
        if (method == null || !method.HasBody)
            return 0;

        var instructions = method.Body.Instructions;
        var il = method.Body.GetILProcessor();
        int patchCount = 0;

        // Find all newobj Vector3 instructions followed by ScreenPointToRay
        for (int i = 0; i < instructions.Count; i++)
        {
            var instr = instructions[i];

            if (instr.OpCode != OpCodes.Newobj)
                continue;

            var methodRef = instr.Operand as MethodReference;
            if (methodRef == null ||
                methodRef.DeclaringType.Name != "Vector3" ||
                methodRef.Parameters.Count != 3)
                continue;

            // Check if next instruction is ScreenPointToRay
            if (i + 1 >= instructions.Count)
                continue;

            var nextInstr = instructions[i + 1];
            if (nextInstr.OpCode != OpCodes.Callvirt && nextInstr.OpCode != OpCodes.Call)
                continue;

            var nextMethodRef = nextInstr.Operand as MethodReference;
            if (nextMethodRef == null || nextMethodRef.Name != "ScreenPointToRay")
                continue;

            // Found the pattern - trace back to find Screen.get_width
            int startIdx = -1;
            for (int j = i - 1; j >= 0 && j >= i - 15; j--)
            {
                if (instructions[j].OpCode == OpCodes.Call)
                {
                    var callRef = instructions[j].Operand as MethodReference;
                    if (callRef != null &&
                        callRef.Name == "get_width" &&
                        callRef.DeclaringType.Name == "Screen")
                    {
                        startIdx = j;
                        break;
                    }
                }
            }

            if (startIdx < 0)
                continue;

            // Remove instructions from startIdx to i (inclusive of newobj)
            // and replace with call to replacement method
            var instructionsToRemove = new List<Instruction>();
            for (int k = startIdx; k <= i; k++)
            {
                instructionsToRemove.Add(instructions[k]);
            }

            // Insert the call before removing to preserve instruction references
            var newCall = il.Create(OpCodes.Call, replacementMethod);
            il.InsertBefore(instructions[startIdx], newCall);

            // Anything branching into the run we are about to delete (the raycast
            // may sit inside an `if`) has to land on the replacement call instead.
            RetargetReferences(method.Body, instructionsToRemove, newCall);

            // Remove the old instructions
            foreach (var toRemove in instructionsToRemove)
            {
                il.Remove(toRemove);
            }

            patchCount++;
            // Adjust i since we modified the instruction list
            i = startIdx;
        }

        return patchCount;
    }

    /// <summary>
    /// Creates a MethodDefinition for a new method (if needed).
    /// </summary>
    public static MethodDefinition CreateVoidMethod(AssemblyDefinition assembly, string name, MethodAttributes attributes)
    {
        var method = new MethodDefinition(
            name,
            attributes,
            assembly.MainModule.TypeSystem.Void);
        method.Body.Instructions.Add(Instruction.Create(OpCodes.Ret));
        return method;
    }
}
"@
}

<#
.SYNOPSIS
    Compiles and loads the screen center patcher code.
.DESCRIPTION
    Compiles the C# patcher code into an in-memory assembly that can be used
    to patch game assemblies.
.PARAMETER CecilPath
    Path to Mono.Cecil.dll (required for compilation).
.OUTPUTS
    The compiled patcher type.
#>
function New-ScreenCenterPatcher {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$false)]
        [string]$CecilPath = $Script:CecilPath,

        [Parameter(Mandatory=$false)]
        [string]$PatchMarker = "HeadTracking_Patched_v2"
    )

    if (-not $CecilPath) {
        throw "Mono.Cecil path not set. Call Initialize-AssemblyPatching first."
    }

    if ($Script:PatcherTypes.ContainsKey($PatchMarker)) {
        return $Script:PatcherTypes[$PatchMarker]
    }

    # One type per marker, so two markers in one session don't collide. A type
    # can't be unloaded, so the name has to differ, not just the cache key.
    #
    # The hash suffix is what actually makes that true. Sanitising alone is not
    # injective - "cul.center" and "cul-center" both flatten to
    # ScreenCenterPatcher_cul_center - and because the lookup below reuses any
    # type it finds BY NAME, the second marker silently got a patcher hard-coded
    # with the first marker's string. The marker is the only thing preventing a
    # double patch, so that means re-patching an assembly that was already done,
    # or skipping one that was not.
    $markerBytes = [System.Text.Encoding]::UTF8.GetBytes($PatchMarker)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $markerHash = $sha.ComputeHash($markerBytes) } finally { $sha.Dispose() }
    $suffix = -join ($markerHash[0..3] | ForEach-Object { '{0:x2}' -f $_ })
    $typeName = 'ScreenCenterPatcher_' + ($PatchMarker -replace '[^A-Za-z0-9_]', '_') + '_' + $suffix
    $code = Get-ScreenCenterPatcherCode -PatchMarker $PatchMarker -TypeName $typeName

    # GetTypes() throws ReflectionTypeLoadException on any assembly whose
    # references don't all resolve, which is normal in a session that has loaded
    # Cecil-read game DLLs. GetType(name, false) never throws.
    $existingType = [AppDomain]::CurrentDomain.GetAssemblies() |
        ForEach-Object { $_.GetType($typeName, $false) } |
        Where-Object { $null -ne $_ } |
        Select-Object -First 1

    if (-not $existingType) {
        # Add-Type -CompilerParameters is Windows PowerShell only; PS Core removed
        # it along with the CodeDom compiler. CI runs pwsh, install-time runs 5.1,
        # so both paths have to exist.
        if ($PSVersionTable.PSEdition -eq 'Core') {
            Add-Type -TypeDefinition $code -ReferencedAssemblies $CecilPath
        } else {
            $compilerParams = New-Object System.CodeDom.Compiler.CompilerParameters
            [void]$compilerParams.ReferencedAssemblies.Add($CecilPath)
            [void]$compilerParams.ReferencedAssemblies.Add("System.dll")
            [void]$compilerParams.ReferencedAssemblies.Add("System.Core.dll")
            $compilerParams.CompilerOptions = "/nowarn:1668 /warn:0"
            $compilerParams.TreatWarningsAsErrors = $false
            Add-Type -TypeDefinition $code -CompilerParameters $compilerParams
        }
        $existingType = [AppDomain]::CurrentDomain.GetAssemblies() |
            ForEach-Object { $_.GetType($typeName, $false) } |
            Where-Object { $null -ne $_ } |
            Select-Object -First 1
        if (-not $existingType) {
            throw "Compiled $typeName but the type is not loadable - Add-Type produced no usable assembly."
        }
    }

    $Script:PatcherTypes[$PatchMarker] = $existingType
    return $existingType
}

<#
.SYNOPSIS
    Patches an Assembly-CSharp.dll for head tracking.
.DESCRIPTION
    Performs common patching operations for head tracking mods:
    1. Injects StaticTracker.ApplyTracking() call into a controller method
    2. Patches screen center raycasts to use StaticTracker.GetAimScreenPosition()
    3. Adds patch marker to prevent double-patching
.PARAMETER AssemblyPath
    Path to the Assembly-CSharp.dll to patch.
.PARAMETER ModDllPath
    Path to the mod DLL containing StaticTracker.
.PARAMETER ControllerTypeName
    Name of the controller type to patch (e.g., "FirstPersonPlayerController").
.PARAMETER ControllerMethodName
    Name of the method to inject into (e.g., "LateUpdate").
.PARAMETER RaycastTypeNames
    Array of type names to patch raycast calls in.
.PARAMETER CecilPath
    Path to Mono.Cecil.dll (optional, uses cached path if not provided).
.OUTPUTS
    Hashtable with patching results.
#>
function Invoke-HeadTrackingPatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$AssemblyPath,

        [Parameter(Mandatory=$true)]
        [string]$ModDllPath,

        [Parameter(Mandatory=$true)]
        [string]$ControllerTypeName,

        [Parameter(Mandatory=$false)]
        [string]$ControllerMethodName = "LateUpdate",

        [Parameter(Mandatory=$false)]
        [string[]]$RaycastTypeNames = @(),

        [Parameter(Mandatory=$false)]
        [string]$CecilPath = $Script:CecilPath,

        [Parameter(Mandatory=$false)]
        [string]$PatchMarker = "HeadTracking_Patched_v2"
    )

    $results = @{
        Success = $false
        AlreadyPatched = $false
        InjectedCall = $false
        RaycastPatches = 0
        Errors = @()
    }

    if (-not $CecilPath) {
        $results.Errors += "Mono.Cecil path not set. Call Initialize-AssemblyPatching first."
        return $results
    }

    # A compile failure is a defect in this module or a broken Cecil reference,
    # not a per-assembly patch outcome. Folding it into $results.Errors hid the
    # cross-edition Add-Type breakage behind "Failed to compile patcher".
    $patcher = New-ScreenCenterPatcher -CecilPath $CecilPath -PatchMarker $PatchMarker

    $managedDir = Split-Path -Parent $AssemblyPath

    $resolver = New-Object Mono.Cecil.DefaultAssemblyResolver
    $resolver.AddSearchDirectory($managedDir)

    $readerParams = New-Object Mono.Cecil.ReaderParameters
    $readerParams.AssemblyResolver = $resolver
    $readerParams.ReadWrite = $false
    $readerParams.InMemory = $true

    try {
        # Read assembly into memory
        $assemblyBytes = [System.IO.File]::ReadAllBytes($AssemblyPath)
        $memStream = New-Object System.IO.MemoryStream(,$assemblyBytes)
        $assembly = [Mono.Cecil.AssemblyDefinition]::ReadAssembly($memStream, $readerParams)

        # Check if already patched
        if ($patcher::IsPatched($assembly)) {
            $results.AlreadyPatched = $true
            $results.Success = $true
            Write-Host "  Assembly already patched - skipping" -ForegroundColor Gray
            $assembly.Dispose()
            $memStream.Dispose()
            return $results
        }

        # Read mod assembly to get method references
        $modBytes = [System.IO.File]::ReadAllBytes($ModDllPath)
        $modStream = New-Object System.IO.MemoryStream(,$modBytes)
        $modAssembly = [Mono.Cecil.AssemblyDefinition]::ReadAssembly($modStream, $readerParams)

        $staticTrackerType = $modAssembly.MainModule.Types | Where-Object { $_.Name -eq "StaticTracker" } | Select-Object -First 1
        if (-not $staticTrackerType) {
            $results.Errors += "StaticTracker type not found in mod DLL"
            $modAssembly.Dispose()
            $modStream.Dispose()
            $assembly.Dispose()
            $memStream.Dispose()
            return $results
        }

        $applyMethod = $staticTrackerType.Methods | Where-Object { $_.Name -eq "ApplyTracking" -and $_.IsStatic } | Select-Object -First 1
        if (-not $applyMethod) {
            $results.Errors += "StaticTracker.ApplyTracking() not found"
            $modAssembly.Dispose()
            $modStream.Dispose()
            $assembly.Dispose()
            $memStream.Dispose()
            return $results
        }
        $applyTrackingRef = $assembly.MainModule.ImportReference($applyMethod)

        $getAimMethod = $staticTrackerType.Methods | Where-Object { $_.Name -eq "GetAimScreenPosition" -and $_.IsStatic } | Select-Object -First 1
        $getAimScreenPositionRef = $null
        if ($getAimMethod) {
            $getAimScreenPositionRef = $assembly.MainModule.ImportReference($getAimMethod)
        }

        $modAssembly.Dispose()
        $modStream.Dispose()

        # Find controller and inject call
        $controllerType = $assembly.MainModule.Types | Where-Object { $_.Name -eq $ControllerTypeName } | Select-Object -First 1
        if ($controllerType) {
            $targetMethod = $controllerType.Methods | Where-Object { $_.Name -eq $ControllerMethodName -and -not $_.IsStatic -and $_.HasBody } | Select-Object -First 1

            if (-not $targetMethod) {
                # Try Update as fallback
                $targetMethod = $controllerType.Methods | Where-Object { $_.Name -eq "Update" -and -not $_.IsStatic -and $_.HasBody } | Select-Object -First 1
            }

            if ($targetMethod) {
                if ($patcher::InjectMethodCall($targetMethod, $applyTrackingRef)) {
                    $results.InjectedCall = $true
                    Write-Host "  Injected StaticTracker.ApplyTracking() into ${ControllerTypeName}.$($targetMethod.Name)" -ForegroundColor Green
                }
            } else {
                Write-Host "  Warning: Could not find suitable method in $ControllerTypeName" -ForegroundColor Yellow
            }
        } else {
            Write-Host "  Warning: $ControllerTypeName not found" -ForegroundColor Yellow
        }

        # Patch raycast calls
        if ($getAimScreenPositionRef -and $RaycastTypeNames.Count -gt 0) {
            foreach ($typeName in $RaycastTypeNames) {
                $type = $assembly.MainModule.Types | Where-Object { $_.Name -eq $typeName } | Select-Object -First 1
                if ($type) {
                    $updateMethod = $type.Methods | Where-Object { $_.Name -eq "Update" -and $_.HasBody } | Select-Object -First 1
                    if ($updateMethod) {
                        $patches = $patcher::PatchScreenCenterRaycasts($updateMethod, $getAimScreenPositionRef)
                        $results.RaycastPatches += $patches
                        Write-Host "  Patched $patches raycast(s) in ${typeName}.Update" -ForegroundColor Green
                    }
                }
            }
        }

        # Add patch marker
        $patcher::AddPatchMarker($assembly)

        # Write patched assembly
        $assembly.Write($AssemblyPath)
        Write-Host "  Successfully patched $(Split-Path -Leaf $AssemblyPath)" -ForegroundColor Green

        $results.Success = $true
        $assembly.Dispose()
        $memStream.Dispose()

    } catch {
        $results.Errors += "Patching error: $_"
        Write-Host "  Error: $_" -ForegroundColor Red
    }

    return $results
}

# Export functions
Export-ModuleMember -Function @(
    'Initialize-AssemblyPatching',
    'Get-ScreenCenterPatcherCode',
    'New-ScreenCenterPatcher',
    'Invoke-HeadTrackingPatch'
)
