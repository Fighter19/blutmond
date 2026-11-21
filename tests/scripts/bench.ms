class Bench extends Actor;

// Work for the speed comparison between the interpreter and native code (apps/script_bench):
// integer and fixed-point arithmetic, branches, loops and calls, without leaving the script.

var int Result;
var fix Acc;

function int mix(int a, int b) {
  return (a * 31 + b) % 1000003;
}

event spawn() {
  var int s = 1;
  for i in 0..60 {
    s = mix(s, i);
    if s % 3 == 0 { s += i * 2; }
  }
  var fix acc = 0.0;
  for i in 0..80 {
    acc = acc * 0.75 + fix(i);
  }
  Acc = acc;
  Result = s;
}
