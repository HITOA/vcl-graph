/**
*   VCL OUTPUT
*/

in vfloat input;

[DoNotMangle]
out vfloat output;

[NodeProcess]
void Process() {
    output = input;
}