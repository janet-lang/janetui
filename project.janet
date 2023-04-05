(declare-project
  :name "janetui"
  :desc "janet bindings for the simple & portable GUI library libui: https://github.com/andlabs/libui"
  :url "https://github.com/janet-lang/janetui"
  :repo "https://github.com/janet-lang/janetui.git")

(rule "build/janetui.so" ["CMakeLists.txt"]
      (do
        (assert
          (zero?
            (os/execute ["cmake" "-B" "build"] :p)))
        (assert
          (zero?
            (os/execute ["cmake" "--build" "build"] :p)))))

(add-dep "build" "build/janetui.so")
