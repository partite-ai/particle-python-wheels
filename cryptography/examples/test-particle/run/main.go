// run/main.go — self-contained runner for the cryptography test
// particle. Builds the Particlefile.py in the parent dir against
// $PARTICLE_LOCAL_WHEELS, instantiates it under the python runtime
// with ephemeral in-memory credential + kv stores (no keyring /
// dbus required), calls the `version` tool, and prints the
// resulting JSON.
//
// Bypasses the `particle` CLI because the CLI uses
// zalando/go-keyring which needs dbus-launch on Linux — not present
// in every dev container. The runner here exercises exactly the
// same code path as the CLI for the in-process bring-up, just with
// in-memory stores.
//
//   PARTICLE_LOCAL_WHEELS=/workspace/wheels/cryptography/out:/workspace/wheels/cffi/out \
//     go run ./run
package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"path/filepath"

	"github.com/partite-ai/wacogo"

	"github.com/partite-ai/particles/credentials"
	credmem "github.com/partite-ai/particles/credentials/memory"
	"github.com/partite-ai/particles/internal/build"
	"github.com/partite-ai/particles/kv"
	kvmem "github.com/partite-ai/particles/kv/memory"
	"github.com/partite-ai/particles/runtime"
)

func main() {
	if err := run(); err != nil {
		log.Fatalf("FAIL: %v", err)
	}
}

func run() error {
	ctx := context.Background()

	// Source dir = current working directory (where the user ran the
	// runner from). Expected to be the example particle dir
	// containing Particlefile.py.
	srcDir, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("getwd: %w", err)
	}
	if _, err := os.Stat(filepath.Join(srcDir, "Particlefile.py")); err != nil {
		return fmt.Errorf("no Particlefile.py in %s — invoke this from the example particle dir", srcDir)
	}
	fmt.Printf("source: %s\n", srcDir)

	// Build. Honors $PARTICLE_LOCAL_WHEELS by default — see
	// build.Options docs.
	res, err := build.Build(ctx, build.Options{
		Source:      os.DirFS(srcDir),
		NoTypeCheck: true,
	})
	if err != nil {
		return fmt.Errorf("build: %w", err)
	}
	fmt.Println("build: ok")

	// Bring up the runtime with ephemeral in-memory stores. No
	// credentials are required by the test particle anyway, and the
	// kv store stays empty.
	engine := wacogo.NewEngine(ctx)
	defer engine.Close(ctx)
	credMgr, err := credentials.NewManager(ctx, credentials.ManagerConfig{Engine: engine})
	if err != nil {
		return fmt.Errorf("credentials manager: %w", err)
	}
	defer credMgr.Close(ctx)
	kvMgr, err := kv.NewManager(ctx, kv.ManagerConfig{Engine: engine})
	if err != nil {
		return fmt.Errorf("kv manager: %w", err)
	}
	defer kvMgr.Close(ctx)

	rt, err := runtime.New(ctx, runtime.Config{
		Engine: engine, Credentials: credMgr, KV: kvMgr,
	})
	if err != nil {
		return fmt.Errorf("runtime.New: %w", err)
	}
	defer rt.Close(ctx)

	credStore := credmem.New().Scoped("crypto-version-probe")
	kvStore := kvmem.New().Scoped("crypto-version-probe")
	p, err := rt.NewParticle(ctx, res.Particle, credStore, kvStore)
	if err != nil {
		return fmt.Errorf("NewParticle: %w", err)
	}
	defer p.Close(ctx)

	fmt.Println("particle: instantiated")

	tool := "version"
	args := `{}`
	if len(os.Args) >= 2 {
		tool = os.Args[1]
	}
	if len(os.Args) >= 3 {
		args = os.Args[2]
	}
	out, err := p.CallTool(ctx, tool, []byte(args))
	if err != nil {
		return fmt.Errorf("CallTool %s: %w", tool, err)
	}
	fmt.Printf("%s: %s\n", tool, out)
	return nil
}
