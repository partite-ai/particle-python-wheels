// repro_test.go — reproducer for the dyld+cryptography trap.
//
// Calls `import_rust` on the test particle, which boils down to a
// single `importlib.import_module("cryptography.hazmat.bindings._rust")`
// in Particlefile.py. That triggers cryptography_rust.abi3.so's dlopen
// via our runtime dyld, then runs its PyO3 __pyo3_module_exec, which
// traps inside libpython3.14.so's intern_common → _Py_hashtable_get
// call_indirect with `wasm error: invalid table access`.
//
// What the test does:
//   - Builds the Particlefile.py (PEP 503 local-wheels resolver picks
//     up cryptography + cffi from $PARTICLE_LOCAL_WHEELS).
//   - Spins up the python runtime with in-memory credential + kv stores
//     (no host keyring needed).
//   - Calls the `import_rust` tool.
//   - Asserts that the call fails with the expected trap message — if
//     it ever STARTS succeeding, the test fails loudly so you notice.
//
// Run it:
//
//	cd /workspace/wheels/cryptography/examples/test-particle/run
//	go test -v -run TestImportRustTrap
//
// With the dlv debugger:
//
//	dlv test -- -test.v -test.run TestImportRustTrap
//
// Good breakpoints once attached:
//   - github.com/tetratelabs/wazero/internal/engine/wazevo.(*moduleEngine).LookupFunction
//     (the slow-path trap site for null-funcref / OOB call_indirect)
//   - github.com/partite-ai/particles/internal/runtime/dyld.(*Adapter).load
//     (cryptography.so's dlopen sequence; the dyld probes print
//     intern.ht / table[5687] state to stderr at each step)
package main

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/partite-ai/wacogo"
	"github.com/tetratelabs/wazero"
	"github.com/tetratelabs/wazero/api"
	"github.com/tetratelabs/wazero/experimental"

	"github.com/partite-ai/particles/credentials"
	credmem "github.com/partite-ai/particles/credentials/memory"
	"github.com/partite-ai/particles/internal/build"
	"github.com/partite-ai/particles/kv"
	kvmem "github.com/partite-ai/particles/kv/memory"
	"github.com/partite-ai/particles/runtime"
)

// Wheels we need on disk. The Makefile in wheels/cryptography produces
// the first; wheels/cffi the second. If either is missing the test
// fails before doing any work — the trap repro only makes sense with
// the matching wheels installed.
var requiredWheels = []string{
	"/Users/mpoindexter/dev/particle/wheels/cryptography/out/cryptography-48.0.0-cp314-abi3-any.whl",
	"/Users/mpoindexter/dev/particle/wheels/cffi/out/cffi-2.0.0-py3-none-any.whl",
}

// TestImportRustTrap exercises the failing path. It expects a trap; if
// you fix the underlying bug, the assertion below will fail with a
// clear message telling you to update the test.
func TestImportRustTrap(t *testing.T) {
	for _, w := range requiredWheels {
		if _, err := os.Stat(w); err != nil {
			t.Fatalf("required wheel missing: %s (build with `make -C wheels/cryptography wheel` "+
				"and `make -C wheels/cffi stub-wheel`)", w)
		}
	}

	// internal/build.scanLocalWheels reads $PARTICLE_LOCAL_WHEELS.
	// Set it for the duration of this test; honor what the user
	// already exported if they did so manually.
	const wantWheelDirs = "/Users/mpoindexter/dev/particle/wheels/cryptography/out:/Users/mpoindexter/dev/particle/wheels/cffi/out"
	if existing := os.Getenv("PARTICLE_LOCAL_WHEELS"); existing == "" {
		t.Setenv("PARTICLE_LOCAL_WHEELS", wantWheelDirs)
	}

	ctx := context.Background()

	// Particlefile.py lives one directory up from this test file.
	srcDir, err := filepath.Abs("..")
	if err != nil {
		t.Fatalf("abs: %v", err)
	}
	if _, err := os.Stat(filepath.Join(srcDir, "Particlefile.py")); err != nil {
		t.Fatalf("no Particlefile.py in %s: %v", srcDir, err)
	}
	t.Logf("source: %s", srcDir)

	res, err := build.Build(ctx, build.Options{
		Source:      os.DirFS(srcDir),
		NoTypeCheck: true,
	})
	if err != nil {
		t.Fatalf("build: %v", err)
	}
	t.Log("build: ok")

	// Use wazero's INTERPRETER engine instead of the default compiler.
	// The compiler JIT-generates native machine code so dlv can't read
	// the wasm operand stack — the trap site has no inspectable state
	// at the host level. The interpreter runs wasm as a Go loop where
	// every operand value is a plain Go variable; setting a breakpoint
	// in wazero's interpreter package lets you read the actual table
	// index at trap time.
	//
	// To step through the trap:
	//   1. dlv test -- -test.v -test.run TestImportRustTrap
	//   2. In dlv:
	//        (dlv) break github.com/tetratelabs/wazero/internal/engine/interpreter.(*callEngine).functionForOffset
	//      That's the function the interpreter calls from operationKindCallIndirect
	//      (interpreter.go:935) — it does the bounds + null check and panics
	//      with wasmruntime.ErrRuntimeInvalidTableAccess. Breakpoint catches
	//      every indirect call; trigger `continue` until the trap one.
	//   3. Once stopped, locals to inspect:
	//        - `offset uint64` — the table index for THIS call_indirect
	//        - `table *wasm.TableInstance` — read `table.References[offset]`
	//          to see whether the slot is genuinely null
	//        - frame stack: `print ce.frames` to see which wasm function
	//          we're inside (look for `_Py_hashtable_get` vs
	//          `_Py_hashtable_get_entry_generic`)
	//   4. To dump the wasm operand stack values from inside the frame:
	//        (dlv) p ce.stack
	//      The values just before this `popValue()` are the call_indirect's
	//      function arguments (key, ht, etc.) — handy for seeing whether
	//      `ht` matches our probe's intern.ht.
	// wacogo's default config enables CoreFeaturesV2 + the extended-const
	// proposal. WithRuntimeConfig replaces the whole config wholesale, so
	// we have to re-enable both here or python-runtime.wasm fails to
	// compile ("i32.add is not supported in a constant expression").
	interpCfg := wazero.NewRuntimeConfigInterpreter().
		WithCoreFeatures(api.CoreFeaturesV2 | experimental.CoreFeaturesExtendedConst)
	engine := wacogo.NewEngine(ctx, wacogo.WithRuntimeConfig(interpCfg))
	t.Cleanup(func() { engine.Close(ctx) })

	credMgr, err := credentials.NewManager(ctx, credentials.ManagerConfig{Engine: engine})
	if err != nil {
		t.Fatalf("credentials manager: %v", err)
	}
	t.Cleanup(func() { credMgr.Close(ctx) })

	kvMgr, err := kv.NewManager(ctx, kv.ManagerConfig{Engine: engine})
	if err != nil {
		t.Fatalf("kv manager: %v", err)
	}
	t.Cleanup(func() { kvMgr.Close(ctx) })

	rt, err := runtime.New(ctx, runtime.Config{
		Engine: engine, Credentials: credMgr, KV: kvMgr,
	})
	if err != nil {
		t.Fatalf("runtime.New: %v", err)
	}
	t.Cleanup(func() { rt.Close(ctx) })

	credStore := credmem.New().Scoped("crypto-trap-repro")
	kvStore := kvmem.New().Scoped("crypto-trap-repro")
	p, err := rt.NewParticle(ctx, res.Particle, credStore, kvStore)
	if err != nil {
		t.Fatalf("NewParticle: %v", err)
	}
	t.Cleanup(func() { p.Close(ctx) })
	t.Log("particle: instantiated")

	// The trap fires inside cryptography.so's PyO3 module exec.
	// Particlefile.py wraps it in `_probe_intern_ht()` calls so the
	// dyld + python-side probes print before/after state to stderr.
	out, callErr := p.CallTool(ctx, "import_rust", []byte(`{}`))

	if callErr == nil {
		t.Fatalf("expected trap, got success: %s\n"+
			"the underlying bug appears to be fixed — update or remove "+
			"this test to assert the new expected behavior", out)
	}

	const wantMsg = "invalid table access"
	if !strings.Contains(callErr.Error(), wantMsg) {
		t.Fatalf("expected %q in error, got: %v", wantMsg, callErr)
	}

	t.Fatalf("reproduced trap as expected: %v", callErr)
}
