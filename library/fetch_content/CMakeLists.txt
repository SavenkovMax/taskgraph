include(FetchContent)

# Unique Function

# ------------------------------------------------------------
FetchContent_Declare(
	function2
	GIT_REPOSITORY https://github.com/Naios/function2.git
	GIT_TAG 4.2.4
)
FetchContent_MakeAvailable(function2)

# -------------------------------------------------------------

# Unit testing

#--------------------------------------------------------------

#FetchContent_Declare(
#	Catch2
#	GIT_REPOSITORY https://github.com/catchorg/Catch2.git	
#	GIT_TAG v3.8.0
#)
#FetchContent_MakeAvailable(Catch2)

#--------------------------------------------------------------
