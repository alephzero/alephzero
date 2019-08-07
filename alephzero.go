package alephzero

// #cgo CFLAGS: -Iinclude
// #include "go/alephzero_go.h"
import "C"

import (
	"log"
)

func Thing() {
	log.Printf("THING")
}
