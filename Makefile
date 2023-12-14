.PHONY: astyle

default:
	# The default target does nothing
	@exit 0

astyle:
	astyle --options=.astylerc \
		--suffix=none \
		--exclude=cores/arduino/api \
		--recursive "cores/*.c,*.cpp,*.h" 
	astyle --options=.astylerc \
		--suffix=none \
		--recursive "libraries/*.c,*.cpp,*.h" 


update-submodules: checkout-submodules
	@echo "Kaleidoscope has been updated from GitHub"

checkout-submodules: git-pull
	git submodule update --init --recursive



maintainer-update-submodules:
	git submodule update --recursive --remote --init
	git submodule foreach git checkout master
	git submodule foreach git pull origin master

git-pull:
	git pull

blindly-commit-updates: git-pull maintainer-update-submodules
	git commit -a -m 'Blindly pull all submodules up to current'
	git push

