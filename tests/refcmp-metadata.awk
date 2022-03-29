# Compare metadata filter output containing float value strings to reference
# output data. Returns the whole reference data if delta of each value is below
# threshold, else returns the whole input data.

function abs(val) {
    return ((val < 0.0) ? -val : val);
}

function max(val1, val2) {
    return ((val1 >= val2) ? val1 : val2);
}

function is_numeric_str(str) {
    return (str ~ /^[+-]?[0-9]*\.?[0-9]+$/);
}

BEGIN {
    FS = "=";
    # check for "fuzz" (threshold) program parameter, else use default
    if (fuzz <= 0.0) {
        fuzz = 0.1;
    }
    # check for "ref" (reference file) program parameter
    if (ref) {
        ref_nr = 0;
        while ((getline line < ref) > 0) {
            ref_nr++;
            ref_lines[ref_nr] = line;
            if (split(line, fields) == 2 && is_numeric_str(fields[2])) {
                ref_keys[ref_nr] = fields[1];
                ref_vals[ref_nr] = fields[2] + 0;  # convert to number
            }
        }
        close(ref);
    }
    delta_max = 0;
    result = (ref ? 1 : 0);
}

{
    cmp_lines[NR] = $0;
    if (NF == 2 && is_numeric_str($2) && ref_vals[NR]) {
        val = $2 + 0;  # convert to number
        delta = abs((val / ref_vals[NR]) - 1);
        delta_max = max(delta_max, delta);
        result = result && ($1 == ref_keys[NR]) && (delta <= fuzz);
    } else {
        result = result && ($0 == ref_lines[NR]);
    }
}

END {
    result = result && (NR == ref_nr);
    if (result) {
        for (i = 1; i <= ref_nr; i++)
            print ref_lines[i];
    } else {
        for (i = 1; i <= NR; i++)
            print cmp_lines[i];
        if (NR == 0)
            print "[refcmp] no input" > "/dev/stderr";
        else if (NR != ref_nr)
            print "[refcmp] lines: " NR " != " ref_nr > "/dev/stderr";
        if (delta_max >= fuzz)
            print "[refcmp] delta_max: " delta_max " >= " fuzz > "/dev/stderr";
    }
}
