-- Names Haskell reserves or already uses (--language haskell on tests/haskell_full_names.hpp):
-- keywords become ordinary camelCase names behind the class prefix, classes named like Prelude
-- types (Maybe, Either, String) and the runtime's types (Handle, Value, CppError) coexist with
-- them, free functions spelled like generated ones get a prime, and conversions of integers,
-- tuples, sets, maps, Maybe, functions, IORefs, policy conversions and modules, exception kinds.
module Main (main) where

import Prelude hiding (Either, Maybe, String, error, fail, id, length, map, maybe, return, show)
import qualified Prelude as P
import Control.Exception (try)
import Control.Monad (unless, when)
import Data.IORef
import qualified Data.Map as M
import qualified Data.Set as S
import Data.Word (Word64)
import System.Environment (getArgs, getExecutablePath)
import System.Exit (ExitCode(..), exitFailure, exitSuccess)
import System.IO (hFlush, hPutStrLn, stderr, stdout)
import System.Mem (performMajorGC)
import System.Process (readProcessWithExitCode)
import Names

main :: IO ()
main = do
  args <- getArgs
  -- Run as `names --interleave` (by the check below, output to a pipe): Haskell and C++ output mixed.
  when (args == ["--interleave"]) $ do
    putStr "a\n"
    sayBuffered "b\n"
    putStr "c\n"
    sayAround (putStr "inside\n")
    sayBuffered "d\n"
    exitSuccess
  counter <- newIORef (0 :: Int)
  let check what ok = do
        modifyIORef' counter (+ 1)
        unless ok $ hPutStrLn stderr ("check failed: " ++ what) >> exitFailure
      fails :: P.String -> ErrorKind -> P.String -> IO a -> IO ()
      fails what kind message action = do
        outcome <- try action
        check what $ case outcome of
          Left (CppError k m) -> k == kind && (null message || m == message)
          Right _ -> False

  -- Classes named like Prelude types and the runtime's types.
  mv <- newMaybe >>= maybeValue
  el <- newEither >>= eitherLeft
  ir <- newIo >>= ioRun
  check "classes Maybe, Either and IO" (mv == 1 && el == 2 && ir == 3)
  s <- newString
  sc <- stringConcat s "t"
  st <- stringText s
  sl <- length s
  check "class String" (sc == "st" && st == "s" && sl == 1)
  stringSetText s "abc"
  sl3 <- length s
  check "field set on String" (sl3 == 3)
  hi <- newHandle >>= handleId
  va <- newValue >>= valueAmount
  ce <- newCppError' >>= cppErrorCode
  check "classes Handle, Value and CppError" (hi == 4 && va == 5 && ce == 6)
  check "Prelude still reachable" (P.maybe 0 (+ 1) (Just (1 :: Int)) == 2 && P.length "ab" == 2)

  -- Methods named like keywords and Prelude values.
  w <- newWords
  r1 <- sequence [wordsType w, wordsData w, wordsLet w, wordsIn w, wordsWhere w, wordsOf w, wordsCase w, wordsModule w]
  r2 <- sequence [wordsImport w, wordsInstance w, wordsDeriving w, wordsNewtype w, wordsForeign w, wordsForall w, wordsMdo w, wordsInfixl w]
  r3 <- sequence [wordsThen w, wordsMap w, wordsShow w]
  check "keyword methods" (r1 == [1 .. 8] && r2 == [9 .. 16] && r3 == [17, 18, 19])
  cc1 <- wordsCamelCase w
  cc2 <- wordsCamelCase' w
  hs <- wordsHttpServer w
  cr <- wordsCreate
  check "camelCase and camel_case both kept" (cc1 == 20 && cc2 == 21 && hs == 22 && cr == 23)
  closeObject w
  wc <- isClosed w
  check "close" wc
  fails "closed object" BindingError "" (wordsType w)
  r <- newRecord
  rt <- recordType r
  rc <- recordClass r
  rd <- recordDefault r
  check "keyword fields" (rt == 1 && rc == 2 && rd == "v")
  recordSetType r 10
  recordSetDefault r "w"
  rt' <- recordType r
  rd' <- recordDefault r
  check "keyword field setters" (rt' == 10 && rd' == "w")
  m1 <- recordMethod r 1
  m2 <- recordMethodWithIn r 1 10
  check "keyword parameters" (m1 == 4 && m2 == 11)
  -- Free functions spelled like generated ones get a prime; the class keeps its names.
  f1 <- newWords'
  f2 <- wordsType'
  f3 <- toWords'
  f4 <- recordSetType'
  check "free functions named like generated ones" (f1 == 30 && f2 == 31 && f3 == 32 && f4 == 33)

  -- Enumerators: prefixed with the enumeration's name.
  t1 <- tokenValue TokenType
  t2 <- tokenValue TokenTrue
  t3 <- tokenValue TokenNothing
  check "keyword enumerators" (t1 == 0 && t2 == 2 && t3 == 3)
  n1 <- nextToken TokenJust
  n2 <- nextToken TokenLowerCase
  check "enum round trip" (n1 == TokenLowerCase && n2 == TokenType)
  check "enum names" (enumName TokenTrue == "True" && enumFromName "Nothing" == Just TokenNothing)
  check "aliases" (enumValue ColorBlue == 6 && enumFromValue 0 == Just ColorRed && enumValue ColorCrimson == 0)
  c1 <- nextColor ColorRed
  c2 <- nextColor ColorCrimson
  check "alias argument" (c1 == ColorGreen && c2 == ColorGreen)
  check "missing value" ((enumFromValue 3 :: P.Maybe Color) == Nothing)
  check "all" (P.length (enumAll :: [Token]) == 6 && [minBound .. maxBound] == (enumAll :: [Token]))
  mo <- modeValue ModeOn
  check "enumerator spelled like its prefix" (mo == 0 && enumValue ModeOff == 1)

  -- Functions named like Prelude values shadow them in Names only.
  p1 <- map 2
  p2 <- show 3
  p3 <- return 4
  p4 <- id 5
  p5 <- error 2
  sm <- statsMap 1.5
  check "Prelude names" (p1 == 4 && p2 == "<3>" && p3 == 5 && p4 == -5 && p5 == 6 && sm == 101)
  check "constants" (maxSize == 64 && then' == 0.5)

  -- Conversions.
  u <- unique [3, 1, 3]
  check "set" (u == S.fromList [1, 3])
  nm <- names
  check "map" (nm == M.fromList [(1, "one"), (2, "two")])
  tr <- triple
  check "tuple" (tr == (1, "two", True))
  ne <- nested
  check "nested pair" (ne == ("n", [1, 2]))
  my1 <- maybe (Just "x")
  my2 <- maybe Nothing
  check "Maybe" (my1 == Just "x" && my2 == Nothing)
  mul <- multiplier
  m42 <- mul 6 7
  check "returned function" (m42 == 42)
  fo1 <- fold [1, 2, 3] 10 (\a b -> P.return (a + b))
  fo2 <- fold [2, 3] 1 (\a b -> P.return (a * b))
  check "two-argument callback" (fo1 == 16 && fo2 == 6)
  seen <- newIORef []
  each [1, 2] (\v -> modifyIORef seen (v :))
  sn <- readIORef seen
  check "unit callback" (sn == [2, 1])
  ct <- callTwice count
  check "zero-argument callback" (ct > 0)
  i1 <- echoI64 (-5)
  i2 <- echoI64 maxBound
  i3 <- smallest
  u8 <- echoU8 200
  check "integers" (i1 == -5 && i2 == maxBound && i3 == minBound && u8 == 200)
  b <- big
  b2 <- echoU64 (maxBound - 1)
  check "uint64 is Word64" (b == (maxBound :: Word64) && b2 == maxBound - 1)
  fails "uint8 range" BindingError "" (echoU8 300)
  fails "unsigned range" BindingError "" (echoU8 (-1))
  e1 <- echo "hé"
  e2 <- withNul
  check "strings" (e1 == "hé" && e2 == "a\0b")
  a <- newIORef 1
  b' <- newIORef 2
  swap a b'
  av <- readIORef a
  bv <- readIORef b'
  check "IORefs" (av == 2 && bv == 1)
  txt <- newIORef "a"
  append txt "b"
  tv <- readIORef txt
  check "string IORef" (tv == "ab")
  to <- total [1, 2, 3]
  dd <- doubled [1, 2]
  check "policy conversion" (to == 6 && dd == [2, 4])
  me <- statsMean [1, 3]
  check "policy module" (me == 2)
  dep <- newOuter >>= outerInner >>= outerInnerDepth
  check "nested class" (dep == 2)

  -- Exceptions.
  fails "invalid_argument" InvalidArgument "negative" (fail (-1))
  fails "runtime_error" NativeError "zero" (fail 0)
  fails "overflow_error" ArithmeticError "one" (fail 1)
  fails "domain_error" DomainError "two" (fail 2)
  fails "length_error" LengthError "three" (fail 3)
  fails "out_of_range" OutOfRange "four" (fail 4)
  fails "non-standard exception" NativeError "" (fail 5)
  f6 <- fail 6
  check "no exception" (f6 == 6)

  -- Haskell and C++ buffer standard output separately; piped, they must still come out in order.
  self <- getExecutablePath
  (code, out, _) <- readProcessWithExitCode self ["--interleave"] ""
  check ("Haskell and C++ output in order: " ++ P.show out) (code == ExitSuccess && out == "a\nb\nc\nbefore\ninside\nafter\nd\n")
  putStr "["
  hFlush stdout
  say "cout"
  putStr "]\n"
  performMajorGC
  n <- readIORef counter
  putStrLn ("Haskell names passed (" ++ P.show n ++ " checks)")
