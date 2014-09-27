function err(msg) {
    status=1
    print FILENAME ":" FNR ": " msg
}

/\s$/ {
    err("whitespace at end")
}

/^    / {
    err("spaces for indent")
}

length($0) > 80 {
    err("line too long")
}

END {
    exit status
}
