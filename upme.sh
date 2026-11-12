
mydir=$(dirname $(realpath $0))
cd "$mydir" && git pull --ff --rebase
