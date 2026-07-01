// fmt_fixture.ls — messy-on-purpose input for the `ls fmt` regression test.
// Deliberately bad spacing / indentation / dangling braces; must still run
// identically after formatting (parse-equivalence) and format idempotently.
import std.core.vec

def   add(int a,int b)->int{
return a+b }


def main(){
Vec(int) v={}
int i=0
while i<5{ v.push(i*2)
i=i+1 }
int s=0
for x in v { s=s+add(x,0) }   // sum 0+2+4+6+8 = 20
if s==20 { @print("FMT_FIXTURE OK") } else { @print("FMT_FIXTURE FAIL") }
}
