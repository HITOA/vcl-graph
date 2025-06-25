/**
*   VCL GAIN
*/

in vfloat input;
in vfloat gain;

out vfloat output;

[NodeProcess]
void Process() {
    output = input * gain;
}