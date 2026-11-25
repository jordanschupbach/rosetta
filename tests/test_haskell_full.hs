-- Haskell API (--language haskell) on the library-shaped example shared with the Python, Lua, Lisp,
-- Julia, C#, Rust, OCaml and Prolog backends. Argument: the generated Features.hs (for the Haddock
-- checks). Run it with +RTS -A64k to stress the garbage collector.
{-# LANGUAGE ScopedTypeVariables #-}
module Main (main) where

import Prelude hiding (repeat, sum)
import Control.Exception (Exception, SomeException, throwIO, try, evaluate, displayException)
import Control.Monad (forM_, unless, when)
import Data.Char (toLower, toUpper)
import Data.IORef
import Data.List (isInfixOf, isPrefixOf, tails)
import qualified Data.ByteString as B
import qualified Data.Map as M
import System.Environment (getArgs)
import System.Exit (exitFailure)
import System.IO (hPutStrLn, stderr)
import System.Mem (performMajorGC)
import Features

data Mine = Mine Int deriving Show
instance Exception Mine

main :: IO ()
main = do
  [path] <- getArgs
  source <- readFile path
  counter <- newIORef (0 :: Int)
  let count = modifyIORef' counter (+ 1)
      check what ok = do
        count
        unless ok $ hPutStrLn stderr ("check failed: " ++ what) >> exitFailure
      -- The action throws a CppError of `kind` whose message contains `pattern`.
      fails :: String -> ErrorKind -> String -> IO a -> IO ()
      fails what kind pattern action = do
        count
        outcome <- try action
        case outcome of
          Left (CppError k m) | k == kind && pattern `isInfixOf` m -> return ()
          Left e -> hPutStrLn stderr (what ++ ": unexpected error " ++ show e) >> exitFailure
          Right _ -> hPutStrLn stderr (what ++ ": expected an error") >> exitFailure
      collect = performMajorGC >> performMajorGC
      feq a b = abs (a - b) < 1e-9 :: Bool
      -- The Haddock comment right above the declaration of `declaration` (a name, or "newtype T").
      doc declaration =
        let ls = lines source
            declares l = l == declaration || (declaration ++ " ") `isPrefixOf` l
            index = length (takeWhile (not . declares) ls)
            above = reverse (takeWhile ("--" `isPrefixOf`) (reverse (take index ls)))
        in if index >= length ls then error ("not declared: " ++ declaration) else unlines above
      contains text pattern = any (pattern `isPrefixOf`) (tails text)

  -- Classes, inheritance and virtual dispatch.
  c1 <- newCircle 2
  a1 <- circleArea c1
  n1 <- shapeName c1
  t1 <- shapeTwiceArea c1
  r1 <- circleRadius c1
  check "circle methods" (a1 == 12 && n1 == "circle" && t1 == 24 && r1 == 2)
  sa <- shapeArea (toShape c1)
  check "upcast" (sa == 12)
  fails "negative radius" InvalidArgument "negative radius" (newCircle (-1))
  s <- newSquare
  side <- squareSide s
  check "default constructor" (side == 1)
  squareSetSide s 3
  s9 <- squareArea s
  s4 <- newSquareSide 2 >>= squareArea
  check "field set, overloaded constructor" (s9 == 9 && s4 == 4)
  ao <- areaOf s
  dc <- describe c1
  check "functions on classes" (ao == 9 && dc == "circle:0")
  -- Enumerations.
  k1 <- shapeKind c1
  k2 <- shapeKind s
  check "enum results" (k1 == KindCircle && k2 == KindSquare)
  f1 <- filled FillSolid
  f2 <- filled FillNone
  check "enum arguments" (f1 && not f2)
  check "enum values" (enumValue KindCustom == 7 && enumFromValue 1 == Just KindSquare && length (enumAll :: [Kind]) == 3)
  check "enum names" (enumName KindCustom == "custom" && enumFromName "circle" == Just KindCircle && (enumFromValue 5 :: Maybe Kind) == Nothing)
  check "qualified enum" (FeaturesLegacyModeOff `elem` [minBound .. maxBound] && enumValue LegacyB == 2)
  check "constants" (shapeSidesUnknown == -1 && feq golden 1.618)

  -- Most-derived results, ownership transfer and shared ownership.
  made <- makeShape KindCircle 1
  case downcast made :: Maybe Circle of
    Just c -> circleRadius c >>= \r -> check "most-derived result" (r == 1)
    Nothing -> check "most-derived result" False
  check "downcast checks the class" (null (downcast made :: Maybe Square) && not (null (downcast made :: Maybe Shape)))
  canvas <- newCanvas
  makeShape KindSquare 2 >>= canvasAdd canvas
  sharedSquare 3 >>= canvasShare canvas
  transferred <- newCircle 1
  canvasAdd canvas transferred
  fails "transferred object is closed" BindingError "closed" (circleArea transferred)
  sz <- canvasSize canvas
  tot <- canvasTotal canvas
  check "ownership transfer" (sz == 3 && tot == 4 + 9 + 3)
  first <- canvasAt canvas 0
  fa <- shapeArea first
  check "borrowed element" (not (null (downcast first :: Maybe Square)) && fa == 4)
  shared <- sharedSquare 2
  canvasShare canvas shared
  sha <- shapeArea shared
  tot2 <- canvasTotal canvas
  check "shared ownership" (sha == 4 && tot2 == 20)
  c11 <- newCircle 1
  ta <- totalArea [toShape c11, shared]
  check "list of objects" (ta == 7)
  fails "std::out_of_range" OutOfRange "" (canvasAt canvas 99)

  -- Overloads: one function per C++ overload, named after its parameters.
  p1 <- pick 1
  p2 <- pickDouble 1.5
  p3 <- pickString "x"
  p4 <- pickListDouble [1, 2]
  p5 <- pickShape s
  check "overloads" (p1 == "int" && p2 == "double" && p3 == "string" && p4 == "vector" && p5 == "shape")
  -- Default arguments: one function per argument count.
  d1 <- add 1
  d2 <- addWithB 1 2
  d3 <- addWithC 1 2 3
  check "default arguments" (d1 == 111 && d2 == 103 && d3 == 6)
  e1 <- defaults
  e2 <- defaultsWithLoud KindCustom "x" True
  e3 <- defaultsWithLambda KindSquare "x" False 0.5 '+' 0
  check "all defaults" (e1 == "x*1-1.500000" && e2 == "x*7!-1.500000" && e3 == "x+10.500000")
  l1 <- shapeLabel c1
  l2 <- shapeLabelWithPrefix c1 "@"
  check "extension with a default argument" (l1 == "#circle" && l2 == "@circle")

  -- Containers.
  counts <- countWords ["a", "b", "a"]
  check "map result" (counts == M.fromList [("a", 2), ("b", 1)])
  mm <- minMax [3, 1, 2]
  check "pair result" (mm == (1, 3))
  fails "empty input" OutOfRange "empty input" (minMax [])
  fd1 <- find [5, 7] 7
  fd2 <- find [5, 7] 9
  check "optional result" (fd1 == Just 1 && fd2 == Nothing)
  tr <- transpose [[1, 2, 3], [4, 5, 6]]
  check "nested lists" (tr == [[1, 4], [2, 5], [3, 6]])
  bs <- byteSum (B.pack [1, 2, 250])
  bo <- bytesOf "AZ"
  check "bytes" (bs == 253 && bo == B.pack [65, 90])
  cs <- circles 3
  r3 <- circleRadius (cs !! 2)
  check "list of class results" (length cs == 3 && r3 == 3)
  rz <- repeat 'z' 3
  i1 <- initial "hello"
  i2 <- initial ""
  check "chars" (rz == "zzz" && i1 == 'h' && i2 == '?')
  -- Mutable reference parameters are IORefs.
  values <- newIORef [1, 2]
  scale values 3
  scaled <- readIORef values
  check "IORef written back" (scaled == [3, 6])
  fails "negative count" InvalidArgument "negative count" (repeat 'z' (-1))
  fails "integer range" BindingError "outside C++ type range" (repeat 'z' maxBound)
  -- Overloads resolve in C++: an Int too large for C++ int picks the double overload.
  pb <- pick maxBound
  check "run-time overload resolution" (pb == "double")
  fu <- defaultsWithLabel KindSquare "ü"
  check "UTF-8 strings" ("ü*1" `isPrefixOf` fu)

  -- Callbacks both ways.
  ap1 <- apply (\x -> return (x * 2)) 21
  check "callback" (ap1 == 42)
  failed <- try (apply (\_ -> throwIO (Mine 7)) 1)
  check "callback exception comes back as itself" (case failed of Left (Mine 7) -> True; _ -> False)
  failed2 <- try (apply (\_ -> evaluate (error "inside callback")) 1)
  check "error call from a callback" (case failed2 of Left (e :: SomeException) -> "inside callback" `isInfixOf` displayException e; _ -> False)
  plus <- adder 40
  p42 <- plus 2
  check "native function as a function" (p42 == 42)
  nested <- apply (\x -> do { f <- adder 1; fromIntegral <$> f (round x) }) 3
  check "native function inside a callback" (nested == 4)
  deep <- apply (\x -> apply (\y -> return (y + 1)) x) 1
  check "nested callbacks" (deep == 2)
  forM_ [1 .. 20 :: Int] $ \i -> do
    v <- apply (\x -> return (x + fromIntegral i)) 0
    check "many callbacks" (v == fromIntegral i)

  -- Value type operators, chaining and borrowed references.
  v <- newVec2XY 1 2
  w <- newVec2XY 3 4 >>= vec2Add v
  wx <- vec2X w
  wy <- vec2Y w
  my <- vec2Multiply v 2 >>= vec2Y
  nx <- vec2Negate v >>= vec2X
  sx <- vec2Subtract v w >>= vec2X
  check "operators" (wx == 4 && wy == 6 && my == 4 && nx == -1 && sx == -3)
  eq1 <- newVec2XY 1 2 >>= vec2Equals v
  eq2 <- vec2Equals v w
  check "operator==" (eq1 && not eq2)
  at1 <- vec2At v 1
  inner <- newVec2XY 1 1 >>= vec2Inner v
  check "operator[] and renamed method" (at1 == 2 && inner == 3)
  fails "Vec2 index" OutOfRange "Vec2 index" (vec2At v 2)
  chained <- vec2SetX v 10 >>= \u -> vec2SetY u 20
  vx <- vec2X v
  vy <- vec2Y v
  check "chaining returns the same object" (vx == 10 && vy == 20 && sameObject chained v)
  vec2SetX' v 5
  vx5 <- vec2X v
  check "field setter beside the set_x method" (vx5 == 5)
  seg <- newSegment
  newVec2XY 3 4 >>= segmentSetEnd seg
  len <- segmentLength2 seg
  check "field object set" (len == 25)
  start <- segmentFirst seg
  _ <- vec2SetX start 3
  stx <- segmentStart seg >>= vec2X
  len16 <- segmentLength2 seg
  check "borrowed reference writes through" (stx == 3 && len16 == 16)
  _ <- segmentStart seg >>= \st -> vec2SetY st 4
  len0 <- segmentLength2 seg
  check "field object borrowed" (len0 == 0)
  cy <- newVec2XY 1 2 >>= vec2Clone >>= vec2Y
  check "clone" (cy == 2)
  ps <- mapM (uncurry newVec2XY) [(1, 2), (3, 4)]
  total <- sum ps
  check "list of values" (total == 10)
  held <- newSegment >>= segmentFirst
  collect
  hx <- vec2X held
  check "borrowed reference keeps its owner alive" (hx == 0)

  -- Templates, extensions, qualified names and copy types.
  stack <- newIntStack
  intStackPush stack 1
  intStackPush stack 2
  ssz <- intStackSize stack
  sit <- intStackItems stack
  spop <- intStackPop stack
  check "template instance" (ssz == 2 && sit == [1, 2] && spop == 2)
  closeObject stack
  fails "pop on a closed object" BindingError "closed" (intStackPop stack)
  closeObject stack
  ci <- clampInt 42
  ch <- clampIntWithHigh 3 5
  cr <- clampReal 2.5
  check "function templates" (ci == 10 && ch == 3 && cr == 2.5)
  ur <- circleUnit >>= circleRadius
  dia <- newCircle 2 >>= circleDiameter
  check "extensions" (ur == 1 && dia == 4)
  lv <- featuresLegacyVersion
  tid <- newFeaturesLegacyTag >>= featuresLegacyTagId
  check "qualified names" (lv == 2 && tid == 7)
  ledger <- newLedger
  copy <- ledgerLatest ledger
  stampSetValue copy 5
  kept <- ledgerStamp ledger >>= stampValue
  check "copy type" (kept == 1)
  aim <- newAim
  _ <- aimOrigin aim >>= \o -> vec2SetX o 2
  px <- aimTarget aim >>= targetPoint >>= vec2X
  check "nested borrowed fields" (px == 2)
  check "read-only field has no setter" (not (contains source "targetSetPoint"))

  -- Errors and closing.
  ce <- try (newCircle (-1))
  check "CppError value" (case ce of Left (CppError InvalidArgument m) -> "negative radius" `isInfixOf` m; _ -> False)
  x <- newCircle 1
  xa <- circleArea x
  closeObject x
  xc <- isClosed x
  check "close" (xa == 3 && xc)
  fails "closed object" BindingError "closed" (circleArea x)
  forM_ [1 .. 200 :: Int] $ \i -> do
    tmp <- makeShape KindSquare (fromIntegral i)
    ar <- shapeArea tmp
    check "many objects" (ar == fromIntegral (i * i))
    when (even i) (closeObject tmp)
  collect

  -- Extended objects: Haskell overrides of C++ virtual methods.
  let triangle b h = extendShape shapeOverrides
        { shapeAreaOverride = Just (\_ -> return (b * h / 2)), shapeNameOverride = Just (\_ -> return "triangle") }
  t <- triangle 4 3
  ta1 <- shapeArea t
  ta2 <- areaOf t
  ta3 <- shapeTwiceArea t
  td <- describe t
  check "Haskell override called from C++" (ta1 == 6 && ta2 == 6 && ta3 == 12 && td == "triangle:7")
  csq <- newSquareSide 2
  mixed <- totalArea [t, toShape c11, toShape csq]
  tl <- shapeLabelWithPrefix t "@"
  check "mixed overrides" (mixed == 13 && tl == "@triangle")
  incomplete <- extendShape shapeOverrides
  fails "pure virtual" BindingError "pure virtual" (areaOf incomplete)
  fails "pure virtual from Haskell" BindingError "pure virtual" (shapeArea incomplete)
  thrower <- extendShape shapeOverrides { shapeAreaOverride = Just (\_ -> throwIO (Mine 3)) }
  thrown <- try (areaOf thrower)
  check "override exception comes back as itself" (case thrown of Left (Mine 3) -> True; _ -> False)
  -- Base functions run the C++ implementation.
  let loud r = extendCircle circleOverrides { circleNameOverride = Just (\self -> map toUpper <$> circleNameBase self) } r
  lc <- loud 1
  ld <- describe lc
  la <- loud 2 >>= circleArea
  ln <- shapeName lc
  check "base function runs the C++ method" (ld == "CIRCLE:0" && la == 12 && ln == "CIRCLE")
  noisy <- extendCircle circleOverrides { circleNameOverride = Just (\self -> (++ "!") . map toLower <$> circleNameBase self) } 1
  nd <- describe noisy
  check "override of a derived class" (nd == "circle!:0")
  kinded <- extendShape shapeOverrides { shapeAreaOverride = Just (\_ -> return 1), shapeKindOverride = Just (\_ -> return KindSquare) }
  kk <- shapeKind kinded
  check "enum result from an override" (kk == KindSquare)
  calls <- newIORef (0 :: Int)
  _ <- extendShape shapeOverrides { shapeAreaOverride = Just (\_ -> modifyIORef calls (+ 1) >> return 1) }
  nc <- readIORef calls
  check "overrides run only when called" (nc == 0)
  canvas2 <- newCanvas
  tag <- newIORef "kept"
  u <- extendShape shapeOverrides { shapeAreaOverride = Just (\_ -> return 1), shapeNameOverride = Just (\_ -> readIORef tag) }
  canvasAdd canvas2 u
  collect
  newSquareSide 2 >>= canvasShare canvas2
  ct <- canvasTotal canvas2
  csz <- canvasSize canvas2
  check "C++ keeps the Haskell object alive" (ct == 5 && csz == 2)
  back <- canvasAt canvas2 0
  back2 <- canvasAt canvas2 0
  bn <- shapeName back
  check "the same Haskell object comes back" (bn == "kept" && sameObject back back2 && sameObject back u)
  canvasClear canvas2
  collect
  extendShape shapeOverrides { shapeAreaOverride = Just (\_ -> return 1) } >>= canvasShare canvas2
  collect
  ct1 <- canvasTotal canvas2
  check "shared extended object" (ct1 == 1)
  canvasClear canvas2
  collect
  seen <- newIORef []
  summer <- extendVisitor visitorOverrides
    { visitorVisitOverride = Just (\_ value -> return (value * 10)), visitorFinishedOverride = Just (\_ vs -> writeIORef seen vs) }
  rv <- runVisitor summer [1, 2]
  sn <- readIORef seen
  check "callback interface" (rv == 30 && sn == [1, 2])
  rd <- newVisitor >>= \vis -> runVisitor vis [1, 2]
  check "native default implementation" (rd == 3)
  rp <- extendVisitor visitorOverrides { visitorFinishedOverride = Just (\_ _ -> return ()) } >>= \vis -> runVisitor vis [1, 2]
  check "partial override keeps the C++ method" (rp == 3)
  forM_ [1 .. 30 :: Int] $ \i -> do
    tri <- triangle (fromIntegral i) 2
    ar <- areaOf tri
    check "many extended objects" (ar == fromIntegral i)
  collect

  -- Policy modules: features::data functions are prefixed data.
  du <- dataUnit
  dux <- vec2X du
  check "submodule result" (dux == 1)
  ds1 <- dataScaled 3
  ds2 <- dataScaledDouble 2.5
  dr <- dataRepeat "ab"
  check "submodule overloads and names" (ds1 == 6 && ds2 == 6.25 && dr == "abab")

  -- Haddock in Features.hs.
  let addDoc = doc "add"
  check "summary" (contains addDoc "Adds three integers, Socha & Dorigo style.")
  check "argument documentation" (contains source "P.Int  -- ^ first term")
  check "returns" (contains source "P.IO P.Int  -- ^ the sum")
  check "defaults documented" (contains addDoc "Arguments left out take the C++ defaults: @b@ = @10@, @c@ = @100@.")
  check "throws" (contains addDoc "Throws t'R.CppError' ('R.OutOfRange') for C++ @std::out\\_of\\_range@: never, in practice.")
  check "code block" (contains addDoc "-- > int total = add(1);  // 111")
  check "C++ signature" (contains addDoc "C++: @int features::add(int a, int b = 10, int c = 100)@")
  check "see" (contains addDoc "See also: 'pick', 'sum'.")
  check "list" (contains addDoc "-- * first item continued")
  check "bold" (contains addDoc "__no__")
  check "constructor exceptions" (contains (doc "newCircle") "'R.InvalidArgument'")
  check "field documentation" (contains (doc "vec2X") "Horizontal component.")
  check "class documentation" (contains (doc "newtype Shape") "Python subclasses may implement")
  check "escapes" (contains (doc "defaults") "{braces}, \\[brackets\\], an \\@ sign")
  check "default summary" (contains (doc "canvasAdd") "-- | Calls C++ @features::Canvas::add@.")
  check "virtual methods documented" (contains (doc "shapeArea") "Virtual:")
  check "submodule docs" (contains (doc "dataUnit") "Mapped by the policy\\'s modules")
  collect
  n <- readIORef counter
  putStrLn ("Haskell full API passed (" ++ show n ++ " checks)")
