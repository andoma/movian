#!/bin/sh

emit() {
    echo -n '<div class="hts-doc-'
    echo -n $1
    echo -n '">'
    echo -n $2
    echo -n ' '
    echo -n $3
    echo '</div>'
    cat $4
}

emit chapter 1     "Overview"                      html/overview.html
emit section 1.1   "List of features"              html/features.html
emit section 1.2   "System requirements"           html/sysreq.html
emit section 1.3   "Install and initial setup"     html/install.html
emit section 1.4   "Default keymappings"           html/keymappings.html
