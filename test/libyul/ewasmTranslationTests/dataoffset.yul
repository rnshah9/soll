// RUN: %soll -lang=Yul %s
// REQUIRES: YulFull
object "main"
{
    code {
        sstore(0, dataoffset("main"))
        sstore(1, dataoffset("sub"))
    }
    object "sub" { code { sstore(0, 1) } }
}
// ----
// Trace:
// Memory dump:
//      0: 0000000000000000000000000000000000000000000000000000000000000001
//     20: 000000000000000000000000000000000000000000000000000000000000070c
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 000000000000000000000000000000000000000000000000000000000000006e
//   0000000000000000000000000000000000000000000000000000000000000001: 000000000000000000000000000000000000000000000000000000000000070c