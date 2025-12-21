-- Sample Lua Application for FMRuby
print("Hello from Lua!")
print("Lua version: " .. _VERSION)
print("Sample Lua app is running successfully")

-- Simple calculation test
local result = 10 + 20
print("Calculation test: 10 + 20 = " .. result)

-- Table test
local fruits = {"apple", "banana", "orange"}
print("Fruits:")
for i, fruit in ipairs(fruits) do
    print("  " .. i .. ": " .. fruit)
end

print("Lua app execution completed")
