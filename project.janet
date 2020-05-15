(declare-project
  :name "janetui"
  :url "https://github.com/janet-lang/janetui"
  :repo "https://github.com/janet-lang/janetui.git")

(rule "build/janetui.so" ["CMakeLists.txt"]
(do
  (os/mkdir "build")
  (os/cd "build")
  (os/execute ["cmake" ".."] :p)
  (assert
    (zero?
      (os/execute ["make"] :p)))))

(add-dep "build" "build/janetui.so")
