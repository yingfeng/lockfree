x = 1:14
y = c(1,1,1,1,1,1,0.895,0.54,0.24,0.08,0.04,0.005,0,0)

# fitting code
fitmodel <- nls(y~a/(1 + exp(-b * (x-c))), start=list(a=1,b=.5,c=9))

# function needed for visualization purposes
sigmoid = function(params, x) {
  params[1] / (1 + exp(-params[2] * (x - params[3])))
}

# visualization code
# get the coefficients using the coef function
params=coef(fitmodel)

y2 <- sigmoid(params,x)
plot(y2,type="l")
points(y)

print.default(params)

